/**
 * @file
 * @brief Now Playing document regression tests.
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

#include <qt/widgets/documents/now_playing_document.hpp>

#include <media/local/resource.hpp>
#include <qt/widgets/tracks/track_table_view.hpp>
#include <queue/playlist.hpp>

#include <QAction>
#include <QApplication>
#include <QFont>
#include <QLabel>
#include <QSignalSpy>
#include <library/in_memory_repository.hpp>
#include <library/location.hpp>
#include <qt/widgets/tracks/track_browser.hpp>

#include <gtest/gtest.h>

#include <algorithm>

namespace {

QAction* action(QWidget& widget, const QString& text)
{
    const QList<QAction*> actions{widget.actions()};
    const auto found = std::ranges::find_if(actions,
        [&text](const QAction* candidate) { return candidate->text() == text; });

    return found == actions.end() ? nullptr : *found;
}

TEST(NowPlayingDocument, PresentsTheActualEngineQueueAndCurrentTrack)
{
    wiola::gui::NowPlayingDocument document;
    wiola::engine::Playlist playlist;
    playlist.set({wiola::media::local_resource("first.wav"),
        wiola::media::local_resource("second.wav")});
    playlist.go_to(1);

    document.show_playlist(playlist);

    QLabel* heading = document.findChild<QLabel*>("nowPlayingHeading");
    wiola::gui::TrackTableView* table = document.findChild<wiola::gui::TrackTableView*>();
    ASSERT_NE(heading, nullptr);
    ASSERT_NE(table, nullptr);
    ASSERT_EQ(table->model()->rowCount(), 2);

    EXPECT_EQ(heading->text(), "Now Playing");
    EXPECT_EQ(table->accessibleName(), "Now Playing tracks");
    EXPECT_FALSE(
        table->model()->data(table->model()->index(0, 1), Qt::FontRole).value<QFont>().bold()
    );
    EXPECT_TRUE(
        table->model()->data(table->model()->index(1, 1), Qt::FontRole).value<QFont>().bold()
    );
}

TEST(NowPlayingDocument, ForwardsIdentityBasedCommandsFromItsTable)
{
    wiola::gui::NowPlayingDocument document;
    wiola::engine::Playlist playlist;
    playlist.set({wiola::media::local_resource("first.wav"),
        wiola::media::local_resource("second.wav")});
    document.show_playlist(playlist);

    wiola::gui::TrackTableView* table = document.findChild<wiola::gui::TrackTableView*>();
    ASSERT_NE(table, nullptr);

    table->selectionModel()->select(table->model()->index(1, 0),
        QItemSelectionModel::Select | QItemSelectionModel::Rows);
    table->selectionModel()->setCurrentIndex(table->model()->index(1, 0),
        QItemSelectionModel::NoUpdate);

    std::optional<wiola::engine::QueueEntryId> chosen;
    int removals{0};
    QObject::connect(&document, &wiola::gui::NowPlayingDocument::track_chosen,
        [&chosen](wiola::engine::QueueEntryId id) { chosen = id; });
    QObject::connect(&document, &wiola::gui::NowPlayingDocument::removal_asked,
        [&removals] { ++removals; });

    QAction* play = action(*table, "Play selected track");
    QAction* remove = action(*table, "Remove selected tracks");
    ASSERT_NE(play, nullptr);
    ASSERT_NE(remove, nullptr);

    play->trigger();
    remove->trigger();

    const auto selected = playlist.tracks()[1].id();
    EXPECT_EQ(chosen, selected);
    EXPECT_EQ(document.chosen_tracks(), std::vector<wiola::engine::QueueEntryId>{selected});
    EXPECT_EQ(removals, 1);
}

TEST(NowPlayingDocument, IdleRefreshAndPlayingMarkerDoNotRebuildOrQueryTheSource)
{
    using namespace wiola;
    std::vector<std::filesystem::path> tracks;
    for (int i = 0; i < 1000; ++i)
        tracks.push_back("/music/" + std::to_string(i) + ".wav");
    engine::Playlist playlist;
    playlist.set(wiola::media::local_resources(tracks));
    gui::NowPlayingDocument document;
    std::size_t resolutions = 0;
    document.set_media_resolver([&](const auto& path) {
        ++resolutions;
        return library::media_id_for(path);
    });
    ASSERT_TRUE(document.show_playlist(playlist));
    auto& table = document.browser().table();
    auto* model = static_cast<gui::TrackTableModel*>(table.model());
    const auto before = model->projection_statistics();
    for (int i = 0; i < 100; ++i)
        EXPECT_FALSE(document.show_playlist(playlist));
    EXPECT_EQ(resolutions, 1000);
    EXPECT_EQ(model->projection_statistics().source_rebuilds, before.source_rebuilds);
    EXPECT_EQ(model->projection_statistics().queries, before.queries);
    QSignalSpy changed{model, &QAbstractItemModel::dataChanged};
    playlist.go_to(1);
    EXPECT_TRUE(document.show_playlist(playlist));
    EXPECT_EQ(changed.size(), 2);
    EXPECT_EQ(model->projection_statistics().queries, before.queries);
    EXPECT_EQ(resolutions, 1000);
}

TEST(NowPlayingDocument, LibraryOverridesRefreshDuplicateOccurrencesWithoutReadingTheFile)
{
    using namespace wiola;
    engine::Playlist playlist;
    playlist.set({wiola::media::local_resource("/missing.wav"),
        wiola::media::local_resource("/missing.wav")});
    library::InMemoryRepository repository;
    library::TrackRecord record;
    record.id = library::media_id_for("/missing.wav");
    record.locations = {"/missing.wav"};
    record.overrides.title = "Application title";
    record.metadata_revision = 1;
    ASSERT_TRUE(repository.put_track(record));
    gui::NowPlayingDocument document;
    document.show_library(repository);
    document.show_playlist(playlist);
    auto* model = document.browser().table().model();
    ASSERT_EQ(model->rowCount(), 2);
    for (int i = 0; i < 2; ++i)
        EXPECT_EQ(model->index(i, 1).data().toString(), "Application title");
}

TEST(NowPlayingDocument,
    LibraryBatchesPatchOnlyMatchingOccurrencesAndKeepRawFieldsUntilLearningCompletes)
{
    using namespace wiola;
    std::vector<std::filesystem::path> tracks;
    for (int i = 0; i < 1000; ++i)
        tracks.push_back("/music/" + std::to_string(i == 1 ? 0 : i) + ".wav");
    engine::Playlist playlist;
    playlist.set(wiola::media::local_resources(tracks));
    playlist.describe(playlist.tracks()[0].id(), {.title = "Raw title", .artist = "Raw artist"});
    library::InMemoryRepository repository;
    gui::NowPlayingDocument document;
    std::size_t lookups = 0;
    document.set_media_resolver([&](const auto& path) {
        ++lookups;
        return library::media_id_for(path);
    });
    document.show_library(repository);
    document.show_playlist(playlist);
    auto* model = static_cast<gui::TrackTableModel*>(document.browser().table().model());
    const auto before = model->projection_statistics();
    library::LibraryChangeBatch batch;
    batch.media = {
        library::MediaChange{.media = {"unrelated"}, .fields = library::MediaChangeFields::tags}
    };
    EXPECT_FALSE(document.show_library_changes(playlist, repository, batch));
    EXPECT_EQ(lookups, 1000);
    EXPECT_EQ(model->projection_statistics().queries, before.queries);
    library::TrackRecord record;
    record.id = library::media_id_for("/music/0.wav");
    record.locations = {"/music/0.wav"};
    record.overrides.title = "Application title";
    record.metadata_revision = 7;
    ASSERT_TRUE(repository.put_track(record));
    batch.media = {
        library::MediaChange{.media = record.id, .fields = library::MediaChangeFields::overrides}
    };
    QSignalSpy changed{model, &QAbstractItemModel::dataChanged};
    document.show_library_changes(playlist, repository, batch);
    EXPECT_EQ(changed.size(), 2);
    EXPECT_EQ(lookups, 1002);
    EXPECT_EQ(model->projection_statistics().source_rebuilds, before.source_rebuilds);
    EXPECT_EQ(model->projection_statistics().queries, before.queries);
    EXPECT_EQ(model->index(0, 1).data().toString(), "Application title");
    EXPECT_EQ(model->index(1, 1).data().toString(), "Application title");
    EXPECT_EQ(model->index(0, 2).data().toString(), "Raw artist");
}

} // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    testing::InitGoogleTest(&argc, argv);
    QApplication application{argc, argv};

    return RUN_ALL_TESTS();
}
