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

#include <algorithm>

namespace {

using namespace wiola;

class ControlledQueue final : public engine::QueueView {
public:
    std::vector<engine::QueueEntry> entries;
    std::optional<std::size_t> selected;
    std::size_t structure{}, metadata{};

    std::span<const engine::QueueEntry> tracks() const noexcept override { return entries; }

    const engine::QueueEntry* current() const noexcept override
    {
        return selected ? &entries[*selected] : nullptr;
    }

    std::optional<std::size_t> position() const noexcept override { return selected; }

    std::optional<std::size_t> index_of(engine::QueueEntryId id) const noexcept override
    {
        const auto found{std::ranges::find(entries, id, &engine::QueueEntry::id)};
        return found == entries.end() ? std::nullopt
                                      : std::optional<std::size_t>{found - entries.begin()};
    }

    std::size_t structural_revision() const noexcept override { return structure; }

    std::size_t metadata_revision() const noexcept override { return metadata; }
};

TEST(TrackRows, LiveReadPortReacquiresEntriesAndRetainsOwnedProjectionFacts)
{
    ControlledQueue queue;
    gui::QueueRows projection;
    EXPECT_TRUE(projection.reconcile(queue, nullptr).structure);
    EXPECT_TRUE(projection.rows().empty());

    media::TrackMetadata facts;
    facts.title = "original";
    queue.entries = {
        {engine::QueueEntryId{11}, "same.wav", facts},
        {engine::QueueEntryId{12}, "same.wav", facts}
    };
    queue.selected = 1;
    ++queue.structure;
    const auto initial{projection.reconcile(queue, nullptr)};
    EXPECT_TRUE(initial.structure);
    EXPECT_TRUE(initial.current);
    ASSERT_EQ(projection.rows().size(), 2U);
    EXPECT_NE(projection.rows()[0].id, projection.rows()[1].id);
    EXPECT_EQ(projection.current(), engine::QueueEntryId{12});
    const auto retained{projection.rows()};
    EXPECT_FALSE(projection.reconcile(queue, nullptr).changed());

    // Replace backing storage without changing occurrence order. A retained span would dangle.
    facts.title = "updated";
    {
        std::vector<engine::QueueEntry> updated{
            {engine::QueueEntryId{11}, "same.wav", facts, {}, 1},
            queue.entries[1]
        };
        queue.entries.swap(updated);
    }
    queue.selected = 0;
    ++queue.metadata;
    const auto changed{projection.reconcile(queue, nullptr)};
    EXPECT_FALSE(changed.structure);
    EXPECT_TRUE(changed.current);
    ASSERT_EQ(changed.rows.size(), 1U);
    EXPECT_EQ(changed.rows.front().id, retained[0].id);
    EXPECT_EQ(changed.rows.front().metadata.title, "updated");
    EXPECT_EQ(retained[0].metadata.title, "original");
    EXPECT_EQ(projection.rows()[1], retained[1]);
    EXPECT_EQ(projection.current(), engine::QueueEntryId{11});
    EXPECT_FALSE(projection.reconcile(queue, nullptr).changed());

    // A different owner can have identical revision numbers and still requires a rebuild.
    ControlledQueue replacement;
    replacement.entries.emplace_back(engine::QueueEntryId{13}, "replacement.wav");
    replacement.selected = 0;
    replacement.structure = queue.structure;
    replacement.metadata = queue.metadata;
    EXPECT_TRUE(projection.reconcile(replacement, nullptr).structure);
    ASSERT_EQ(projection.rows().size(), 1U);
    EXPECT_EQ(projection.current(), engine::QueueEntryId{13});
    EXPECT_EQ(projection.rows().front().location, "replacement.wav");

    replacement.entries.clear();
    replacement.selected.reset();
    ++replacement.structure;
    EXPECT_TRUE(projection.reconcile(replacement, nullptr).structure);
    EXPECT_TRUE(projection.rows().empty());
    EXPECT_FALSE(projection.current());
}

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
    queue.set({"same.wav", "same.wav"});
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
