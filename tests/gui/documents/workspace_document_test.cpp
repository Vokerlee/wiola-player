/**
 * @file
 * @brief Incremental workspace document regression tests.
 * @author Roman Glaz
 * @copyright (c) 2026, <vokerlee@gmail.com>
 *
 * Wiola is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Wiola is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Wiola. If not, see <http://www.gnu.org/licenses/>.
 */

#include <gui/documents/workspace_document.hpp>

#include <library/in_memory_repository.hpp>
#include <library/location.hpp>

#include <QApplication>
#include <QLabel>

#include <gtest/gtest.h>

namespace {

TEST(WorkspaceDocument, AppendsCurrentBatchesAndRejectsStaleGenerations)
{
    wiola::gui::WorkspaceDocument document;
    const wiola::library::WorkspaceRecord workspace{.id =
                                                        wiola::library::WorkspaceId{"workspace-1"},
        .root = "/music",
        .recursive = true};
    document.begin(workspace, 7);

    wiola::gui::TrackTableView* table = document.findChild<wiola::gui::TrackTableView*>();
    ASSERT_NE(table, nullptr);
    ASSERT_TRUE(document.show_batch({.workspace = workspace.id,
        .generation = 7,
        .tracks = {"a.wav"},
        .status = wiola::app::WorkspaceScanStatus::running}));

    table->selectionModel()->select(table->model()->index(0, 0),
        QItemSelectionModel::Select | QItemSelectionModel::Rows);

    ASSERT_TRUE(document.show_batch({.workspace = workspace.id,
        .generation = 7,
        .tracks = {"nested/b.flac"},
        .status = wiola::app::WorkspaceScanStatus::running}));

    EXPECT_EQ(table->model()->rowCount(), 2);
    EXPECT_EQ(document.chosen_tracks().size(), 1);
    EXPECT_EQ(document.findChild<QLabel*>("workspaceStatus")->text(), "2 tracks");

    EXPECT_FALSE(document.show_batch({.workspace = workspace.id,
        .generation = 6,
        .tracks = {"stale.wav"},
        .status = wiola::app::WorkspaceScanStatus::complete}));
    EXPECT_EQ(table->model()->rowCount(), 2);

    ASSERT_TRUE(document.show_batch({.workspace = workspace.id,
        .generation = 7,
        .tracks = {},
        .status = wiola::app::WorkspaceScanStatus::complete}));
    EXPECT_EQ(document.findChild<QLabel*>("workspaceStatus")->text(), "2 tracks");
}

TEST(WorkspaceDocument, ClearsRowsWhenANewScanStarts)
{
    wiola::gui::WorkspaceDocument document;
    const wiola::library::WorkspaceRecord workspace{.id =
                                                        wiola::library::WorkspaceId{"workspace-1"},
        .root = "/music",
        .recursive = false};
    document.begin(workspace, 1);
    ASSERT_TRUE(document.show_batch({.workspace = workspace.id,
        .generation = 1,
        .tracks = {"a.wav"},
        .status = wiola::app::WorkspaceScanStatus::running}));

    document.begin(workspace, 2);

    wiola::gui::TrackTableView* table = document.findChild<wiola::gui::TrackTableView*>();
    EXPECT_EQ(table->model()->rowCount(), 0);
    EXPECT_EQ(document.findChild<QLabel*>("workspaceStatus")->text(), "Scanning...");
}

TEST(WorkspaceDocument, ExposesRecursionRescanProgressAndCancellationControls)
{
    wiola::gui::WorkspaceDocument document;
    document.begin({.id = wiola::library::WorkspaceId{"workspace"},
                       .root = "/music/library",
                       .recursive = true},
        4);

    QCheckBox* recursive = document.findChild<QCheckBox*>("workspaceRecursive");
    QPushButton* rescan = document.findChild<QPushButton*>("rescanWorkspace");
    QPushButton* cancel = document.findChild<QPushButton*>("cancelWorkspaceScan");
    QProgressBar* progress = document.findChild<QProgressBar*>("workspaceProgress");
    ASSERT_NE(recursive, nullptr);
    ASSERT_NE(rescan, nullptr);
    ASSERT_NE(cancel, nullptr);
    ASSERT_NE(progress, nullptr);

    EXPECT_TRUE(recursive->isChecked());
    EXPECT_EQ(document.findChild<QLabel*>("workspaceHeading")->text(), "/music/library");
    EXPECT_EQ(progress->maximum(), 0);
    EXPECT_TRUE(cancel->isEnabled());
    EXPECT_FALSE(rescan->isEnabled());

    std::optional<bool> requested_policy;
    document.on_rescan_requested([&requested_policy](bool policy) { requested_policy = policy; });
    recursive->setChecked(false);
    rescan->setEnabled(true);
    rescan->click();
    EXPECT_EQ(requested_policy, false);

    document.on_cancel_requested([&document] { document.show_cancelled(); });
    cancel->click();
    EXPECT_NE(progress->maximum(), 0);
    EXPECT_FALSE(cancel->isEnabled());
    EXPECT_TRUE(rescan->isEnabled());
    EXPECT_EQ(document.findChild<QLabel*>("workspaceStatus")->text(), "0 tracks — cancelled");
}

TEST(WorkspaceDocument, RoutesSelectedPathsToPlaybackAndTransferCallbacks)
{
    wiola::gui::WorkspaceDocument document;
    const wiola::library::WorkspaceRecord workspace{.id = wiola::library::WorkspaceId{"workspace"},
        .root = "/music",
        .recursive = true};
    document.begin(workspace, 1);
    ASSERT_TRUE(document.show_batch({
        .workspace = workspace.id,
        .generation = 1,
        .tracks = {"one.wav", "two.flac"},
        .status = wiola::app::WorkspaceScanStatus::running
    }));

    wiola::gui::TrackTableView* table = document.findChild<wiola::gui::TrackTableView*>();
    table->selectionModel()->select(table->model()->index(1, 0),
        QItemSelectionModel::Select | QItemSelectionModel::Rows);

    int play = 0;
    int append = 0;
    int active = 0;
    int destination = 0;
    document.on_play_requested([&play] { ++play; });
    document.on_append_requested([&append] { ++append; });
    document.on_transfer_active_requested([&active] { ++active; });
    document.on_transfer_destination_requested([&destination] { ++destination; });

    document.findChild<QPushButton*>("playWorkspaceSelection")->click();
    document.findChild<QPushButton*>("appendWorkspaceSelection")->click();

    for (QAction* action : table->actions()) {
        if (action->text() == "Add to active playlist")
            action->trigger();
        if (action->text() == "Add to playlist →")
            action->trigger();
    }

    EXPECT_EQ(play, 1);
    EXPECT_EQ(append, 1);
    EXPECT_EQ(active, 1);
    EXPECT_EQ(destination, 1);
    EXPECT_EQ(document.chosen_paths(), std::vector<std::filesystem::path>{"two.flac"});
}

TEST(WorkspaceDocument, AppliesMediaRevisionsByIdentityWithoutLosingSelection)
{
    wiola::gui::WorkspaceDocument document;
    const wiola::library::WorkspaceRecord workspace{.id = wiola::library::WorkspaceId{"workspace"},
        .root = "/music",
        .recursive = true};
    document.begin(workspace, 1);
    ASSERT_TRUE(document.show_batch({
        .workspace = workspace.id,
        .generation = 1,
        .tracks = {"/music/one.wav", "/music/two.wav"},
        .status = wiola::app::WorkspaceScanStatus::complete
    }));

    wiola::gui::TrackTableView* table = document.findChild<wiola::gui::TrackTableView*>();
    table->selectionModel()->select(table->model()->index(1, 0),
        QItemSelectionModel::Select | QItemSelectionModel::Rows);
    const std::vector<wiola::engine::QueueEntryId> selected = document.chosen_tracks();

    wiola::library::InMemoryRepository repository;
    wiola::library::TrackRecord record;
    record.id = wiola::library::media_id_for("/music/two.wav");
    record.embedded.title = "Embedded title";
    record.embedded.artist = "Learned artist";
    record.overrides.title = "User title";
    record.tags = {
        {.id = wiola::library::TagId{"tag"}, .key = "mood", .value = "Calm"}
    };
    ASSERT_TRUE(repository.put_track(record));

    ASSERT_TRUE(document.show_media_revision(repository, record.id));

    EXPECT_EQ(table->model()->data(table->model()->index(1, 1), Qt::DisplayRole).toString(),
        "User title");
    EXPECT_EQ(table->model()->data(table->model()->index(1, 2), Qt::DisplayRole).toString(),
        "Learned artist");
    EXPECT_EQ(table->model()->data(table->model()->index(0, 1), Qt::DisplayRole).toString(), "one");
    EXPECT_EQ(document.chosen_tracks(), selected);
    EXPECT_EQ(repository.track(record.id)->tags.size(), 1);
    EXPECT_EQ(repository.track(record.id)->overrides.title, "User title");
    EXPECT_FALSE(document.show_media_revision(repository,
        wiola::library::media_id_for("/music/missing.wav")));
}

} // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    testing::InitGoogleTest(&argc, argv);
    QApplication application{argc, argv};

    return RUN_ALL_TESTS();
}
