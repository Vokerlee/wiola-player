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

#include <qt/widgets/tracks/track_table_view.hpp>

#include <qt/widgets/documents/workspace_document.hpp>

#include <app/library/local_identity.hpp>
#include <library/in_memory_repository.hpp>

#include <QApplication>
#include <QEventLoop>
#include <QLabel>
#include <QTimer>
#include <qt/widgets/tracks/track_browser.hpp>

#include <gtest/gtest.h>

namespace {

std::vector<wiola::app::WorkspaceScanEntry>
entries(std::initializer_list<std::filesystem::path> paths)
{
    std::vector<wiola::app::WorkspaceScanEntry> result;
    for (const auto& path : paths) {
        wiola::app::WorkspaceScanEntry entry;
        entry.media = wiola::app::media_id_for(path);
        entry.location = path;
        result.push_back(std::move(entry));
    }
    return result;
}

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
        .entries = entries({"a.wav"}),
        .status = wiola::app::WorkspaceScanStatus::running}));

    table->selectionModel()->select(table->model()->index(0, 0),
        QItemSelectionModel::Select | QItemSelectionModel::Rows);

    ASSERT_TRUE(document.show_batch({.workspace = workspace.id,
        .generation = 7,
        .entries = entries({"nested/b.flac"}),
        .status = wiola::app::WorkspaceScanStatus::running}));

    EXPECT_EQ(table->model()->rowCount(), 2);
    EXPECT_EQ(document.chosen_rows().size(), 1);
    EXPECT_EQ(document.findChild<QLabel*>("workspaceStatus")->text(), "2 tracks");

    EXPECT_FALSE(document.show_batch({.workspace = workspace.id,
        .generation = 6,
        .entries = entries({"stale.wav"}),
        .status = wiola::app::WorkspaceScanStatus::complete}));
    EXPECT_EQ(table->model()->rowCount(), 2);

    ASSERT_TRUE(document.show_batch({.workspace = workspace.id,
        .generation = 7,
        .entries = entries({}),
        .status = wiola::app::WorkspaceScanStatus::complete}));
    EXPECT_EQ(document.findChild<QLabel*>("workspaceStatus")->text(), "2 tracks");
}

TEST(WorkspaceDocument, RetainsUsefulRowsWhenANewScanStarts)
{
    wiola::gui::WorkspaceDocument document;
    const wiola::library::WorkspaceRecord workspace{.id =
                                                        wiola::library::WorkspaceId{"workspace-1"},
        .root = "/music",
        .recursive = false};
    document.begin(workspace, 1);
    ASSERT_TRUE(document.show_batch({.workspace = workspace.id,
        .generation = 1,
        .entries = entries({"a.wav"}),
        .status = wiola::app::WorkspaceScanStatus::running}));

    document.begin(workspace, 2);

    wiola::gui::TrackTableView* table = document.findChild<wiola::gui::TrackTableView*>();
    EXPECT_EQ(table->model()->rowCount(), 1);
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
    ASSERT_TRUE(document.show_batch({.workspace = workspace.id,
        .generation = 1,
        .entries = entries({"one.wav", "two.flac"}),
        .status = wiola::app::WorkspaceScanStatus::running}));

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
    ASSERT_TRUE(document.show_batch({.workspace = workspace.id,
        .generation = 1,
        .entries = entries({"/music/one.wav", "/music/two.wav"}),
        .status = wiola::app::WorkspaceScanStatus::complete}));

    wiola::gui::TrackTableView* table = document.findChild<wiola::gui::TrackTableView*>();
    table->selectionModel()->select(table->model()->index(1, 0),
        QItemSelectionModel::Select | QItemSelectionModel::Rows);
    const std::vector<wiola::gui::TrackRowId> selected = document.chosen_rows();

    wiola::library::InMemoryRepository repository;
    wiola::library::TrackRecord record;
    record.id = wiola::app::media_id_for("/music/two.wav");
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
    EXPECT_EQ(document.chosen_rows(), selected);
    EXPECT_EQ(repository.track(record.id)->tags.size(), 1);
    EXPECT_EQ(repository.track(record.id)->overrides.title, "User title");
    EXPECT_FALSE(document.show_media_revision(repository,
        wiola::app::media_id_for("/music/missing.wav")));
}

TEST(WorkspaceDocument, RescanAndKnownRelocationKeepTheSelectedMediaIdentity)
{
    wiola::gui::WorkspaceDocument document;
    const wiola::library::WorkspaceRecord workspace{.id = wiola::library::WorkspaceId{"workspace"},
        .root = "/music"};
    wiola::app::WorkspaceScanEntry entry;
    entry.media = {"canonical-media"};
    entry.location = "/music/old.wav";
    document.begin(workspace, 1);
    ASSERT_TRUE(document.show_batch({.workspace = workspace.id,
        .generation = 1,
        .entries = {entry},
        .status = wiola::app::WorkspaceScanStatus::complete}));
    auto* table = document.findChild<wiola::gui::TrackTableView*>();
    table->selectRow(0);
    const auto selected = document.chosen_rows();
    document.begin(workspace, 2);
    EXPECT_EQ(document.chosen_rows(), selected);
    entry.location = "/music/new.wav";
    ASSERT_TRUE(document.show_batch({.workspace = workspace.id,
        .generation = 2,
        .entries = {entry},
        .status = wiola::app::WorkspaceScanStatus::running}));
    ASSERT_TRUE(document.show_batch({.workspace = workspace.id,
        .generation = 2,
        .entries = {},
        .status = wiola::app::WorkspaceScanStatus::complete}));
    EXPECT_EQ(document.chosen_rows(), selected);
    ASSERT_EQ(document.selection().size(), 1);
    EXPECT_EQ(document.selection().front().media, entry.media);
    EXPECT_EQ(document.chosen_paths(), std::vector{entry.location});
    EXPECT_FALSE(document.show_batch({.workspace = workspace.id,
        .generation = 1,
        .entries = {},
        .status = wiola::app::WorkspaceScanStatus::complete}));
}

TEST(WorkspaceDocument, CancellationKeepsUsefulRowsAndRejectsLateCompletion)
{
    wiola::gui::WorkspaceDocument document;
    const wiola::library::WorkspaceRecord workspace{.id = wiola::library::WorkspaceId{"workspace"},
        .root = "/music"};
    document.begin(workspace, 1);
    ASSERT_TRUE(document.show_batch({.workspace = workspace.id,
        .generation = 1,
        .entries = entries({"/music/old.wav"}),
        .status = wiola::app::WorkspaceScanStatus::complete}));
    auto* table = document.findChild<wiola::gui::TrackTableView*>();
    table->selectRow(0);
    const auto selected = document.chosen_rows();
    document.begin(workspace, 2);
    document.show_cancelled();
    EXPECT_EQ(document.chosen_rows(), selected);
    EXPECT_EQ(table->total_count(), 1);
    EXPECT_FALSE(document.show_batch({.workspace = workspace.id,
        .generation = 2,
        .entries = {},
        .status = wiola::app::WorkspaceScanStatus::complete}));
    EXPECT_EQ(table->total_count(), 1);
}

TEST(WorkspaceDocument, RemovesAbsentRowsOnlyAfterTheReplacementGenerationCompletes)
{
    wiola::gui::WorkspaceDocument document;
    const wiola::library::WorkspaceRecord workspace{.id = wiola::library::WorkspaceId{"workspace"},
        .root = "/music"};
    document.begin(workspace, 1);
    ASSERT_TRUE(document.show_batch({.workspace = workspace.id,
        .generation = 1,
        .entries = entries({"/music/old.wav"}),
        .status = wiola::app::WorkspaceScanStatus::complete}));
    auto* table = document.findChild<wiola::gui::TrackTableView*>();
    table->selectRow(0);
    const auto selected = document.chosen_rows();
    document.begin(workspace, 2);
    ASSERT_TRUE(document.show_batch({.workspace = workspace.id,
        .generation = 2,
        .entries = entries({"/music/new.wav"}),
        .status = wiola::app::WorkspaceScanStatus::running}));
    EXPECT_EQ(document.chosen_rows(), selected);
    ASSERT_TRUE(document.show_batch({.workspace = workspace.id,
        .generation = 2,
        .entries = {},
        .status = wiola::app::WorkspaceScanStatus::complete}));
    EXPECT_TRUE(document.chosen_rows().empty());
    EXPECT_EQ(table->total_count(), 1);
}

TEST(WorkspaceDocument, ANewScanKeepsAnExplicitlyClearedTitle)
{
    wiola::library::InMemoryRepository repository;
    wiola::library::TrackRecord record;
    record.id = {"cleared"};
    record.locations = {"/music/cleared.wav"};
    record.overrides.title = "";
    ASSERT_TRUE(repository.put_track(record));
    wiola::app::WorkspaceScanEntry entry;
    entry.media = record.id;
    entry.location = record.locations.front();
    wiola::gui::WorkspaceDocument document;
    const wiola::library::WorkspaceId workspace{"workspace"};
    document.begin({.id = workspace, .root = "/music"}, 1);
    ASSERT_TRUE(document.show_batch({.workspace = workspace,
                                        .generation = 1,
                                        .entries = {entry},
                                        .status = wiola::app::WorkspaceScanStatus::complete},
        &repository));
    const auto* table = document.findChild<wiola::gui::TrackTableView*>();
    EXPECT_TRUE(table->model()->index(0, 1).data().toString().isEmpty());
}

TEST(WorkspaceDocument, StreamsInStablePathOrderAndReportsScanLimitsWithoutDroppingUsefulRows)
{
    wiola::gui::WorkspaceDocument document;
    document.begin({.id = {"workspace"}, .root = "folder"}, 1);
    ASSERT_TRUE(document.show_batch({.workspace = {"workspace"},
        .generation = 1,
        .entries = entries({"folder/z.wav", "folder/a.wav"}),
        .status = wiola::app::WorkspaceScanStatus::running}));
    auto* table = document.findChild<wiola::gui::TrackTableView*>();
    ASSERT_NE(table, nullptr);
    EXPECT_EQ(table->model()->index(0, 1).data().toString(), "a");
    ASSERT_TRUE(document.show_batch({.workspace = {"workspace"},
        .generation = 1,
        .entries = {},
        .status = wiola::app::WorkspaceScanStatus::failed,
        .error = wiola::app::WorkspaceScanError::limit_exceeded}));
    EXPECT_EQ(table->total_count(), 2);
    EXPECT_TRUE(
        document.findChild<QLabel*>("workspaceStatus")->text().contains("Scan limit reached")
    );
}

TEST(WorkspaceDocument, RestoreWaitsForBothTheCompletedScanAndItsLargeQueryProjection)
{
    using namespace wiola;
    gui::WorkspaceDocument document;
    library::WorkspaceRecord workspace{.id = {"workspace"}, .root = "/music"};
    document.begin(workspace, 1);
    app::BrowserViewState restore;
    restore.view.query.title = "track 4999";
    const auto wanted = app::media_id_for("/music/Track 4999.wav");
    restore.selected = {
        gui::WorkspaceTrackId{workspace.id, wanted}
    };
    restore.current = restore.selected[0];
    document.browser().restore_browser_state(restore);
    app::WorkspaceScanBatch batch;
    batch.workspace = workspace.id;
    batch.generation = 1;
    for (int i = 0; i < 5000; ++i) {
        app::WorkspaceScanEntry entry;
        entry.location = "/music/Track " + std::to_string(i) + ".wav";
        entry.media = app::media_id_for(entry.location);
        batch.entries.push_back(std::move(entry));
    }
    ASSERT_TRUE(document.show_batch(batch));
    batch.entries.clear();
    batch.status = app::WorkspaceScanStatus::complete;
    ASSERT_TRUE(document.show_batch(batch));
    auto& table = document.browser().table();
    ASSERT_TRUE(table.projection_pending());
    QEventLoop loop;
    QTimer deadline;
    deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(&table, &gui::TrackTableView::view_state_changed, &loop, [&] {
        if (!table.projection_pending())
            loop.quit();
    });
    deadline.start(10000);
    loop.exec();
    EXPECT_FALSE(table.projection_pending());
    ASSERT_EQ(table.visible_count(), 1);
    EXPECT_EQ(table.chosen_rows(), restore.selected);
    EXPECT_EQ(document.selection()[0].media, wanted);
}

} // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    testing::InitGoogleTest(&argc, argv);
    QApplication application{argc, argv};

    return RUN_ALL_TESTS();
}
