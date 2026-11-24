/**
 * @file
 * @brief Transport-independent track presentation values and explicit library resolution.
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

#include <fixtures/track_view.hpp>

#include <QAction>
#include <QApplication>
#include <gtest/gtest.h>
#include <gui/model/documents/queue_rows.hpp>
#include <gui/tracks/track_table_model.hpp>
#include <gui/tracks/track_table_view.hpp>
#include <queue/playlist.hpp>

namespace {

using namespace wiola;

library::TrackRecord record(std::string id, std::string title, std::string mood)
{
    library::TrackRecord result;
    result.id = {std::move(id)};
    result.locations = {"/not-probed/music/" + result.id.value + ".flac"};
    result.embedded.title = std::move(title);
    result.embedded.artist = "Artist";
    result.tags.push_back({library::TagId{"tag-" + result.id.value}, "Mood", std::move(mood)});
    return result;
}

TEST(TrackRows, NonPlayingRowsKeepTheirLibraryIdentityAndResolvedValues)
{
    auto source = record("opaque-media", "Embedded", "Calm");
    source.overrides.title = "Override";
    source.metadata_revision = 12;
    source.content_revision = 4;
    const library::PlaylistEntryId entry{"occurrence"};
    const auto row = gui::present_record(entry, source);
    EXPECT_EQ(row.media, source.id);
    EXPECT_EQ(std::get<library::PlaylistEntryId>(row.id), entry);
    EXPECT_EQ(row.metadata.title, "Override");
    EXPECT_EQ(row.location, source.locations.front());
    EXPECT_EQ(row.metadata_revision, 12);
    EXPECT_EQ(row.content_revision, 4);
    EXPECT_FALSE(row.missing);
    EXPECT_EQ(row.tags, source.tags);
}

TEST(TrackRows, SortsAndFiltersSavedAndWorkspaceRowsWithoutAPlaybackQueueOrRepository)
{
    auto saved = record("saved-media", "Zulu", "Calm");
    auto workspace = record("workspace-media", "Alpha", "Brisk");
    workspace.missing = true;
    const auto first = gui::present_record(library::PlaylistEntryId{"entry"}, saved);
    const auto second =
        gui::present_record(gui::WorkspaceTrackId{library::WorkspaceId{"workspace"}, workspace.id},
            workspace);
    QWidget styled;
    gui::TrackTableModel model{styled};
    model.show_rows({first, second});
    wiola::testing::set_track_sort(model,
        {
            {.field = gui::TrackSortField::title,
             .direction = gui::TrackSortDirection::ascending,
             .tag_key = {}}
    });
    EXPECT_EQ(model.row_id(model.index(0, 0)), second.id);
    EXPECT_EQ(model.row_id(model.index(1, 0)), first.id);
    EXPECT_TRUE(model.data(model.index(0, 0), Qt::ToolTipRole).toString().contains("unavailable"));
    model.show_tag_keys({"Mood"});
    EXPECT_EQ(model.data(model.index(1, 5), Qt::DisplayRole).toString(), "Calm");
    library::SearchQuery query;
    query.artist = "Artist";
    query.tags = "Calm";
    wiola::testing::set_track_query(model, query);
    ASSERT_EQ(model.rowCount(), 1);
    EXPECT_EQ(model.row_id(model.index(0, 0)), first.id);
}

TEST(TrackRows, SelectionAndActivationRetainDistinctSavedOccurrenceIdentities)
{
    const auto source = record("shared-media", "Shared", "Calm");
    auto first = gui::present_record(library::PlaylistEntryId{"entry-one"}, source);
    auto duplicate = gui::present_record(library::PlaylistEntryId{"entry-two"}, source);
    gui::TrackTableView view;
    view.show_rows({first, duplicate});
    view.selectRow(1);
    view.setCurrentIndex(view.model()->index(1, 0));
    std::optional<gui::TrackRowId> activated;
    QObject::connect(&view, &gui::TrackTableView::row_chosen,
        [&](gui::TrackRowId id) { activated = std::move(id); });
    view.actions().front()->trigger();
    EXPECT_EQ(activated, duplicate.id);
    first.metadata.title = "Updated";
    view.show_rows({duplicate, first});
    EXPECT_EQ(view.chosen_rows(), std::vector{duplicate.id});
    EXPECT_EQ(view.currentIndex().row(), 0);
}

TEST(TrackRows, QueueAdapterKeepsDuplicateOccurrencesAndMediaIdentitySeparate)
{
    engine::Playlist queue;
    queue.set({engine::Track::of("same.wav"), engine::Track::of("same.wav")});
    const auto rows = gui::queue_rows(queue.tracks());
    ASSERT_EQ(rows.size(), 2);
    EXPECT_NE(rows[0].id, rows[1].id);
    EXPECT_EQ(rows[0].media, rows[1].media);
    EXPECT_EQ(std::get<engine::QueueEntryId>(rows[0].id), queue.tracks()[0].id());
    EXPECT_EQ(std::get<engine::QueueEntryId>(rows[1].id), queue.tracks()[1].id());
}

} // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "minimal");
    ::testing::InitGoogleTest(&argc, argv);
    QApplication application{argc, argv};
    return RUN_ALL_TESTS();
}
