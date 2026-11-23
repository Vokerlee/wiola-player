/**
 * @file
 * @brief Stable multi-column track sorting regression tests.
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

#include <gui/documents/queue_rows.hpp>

#include <gui/tracks/track_sorter.hpp>

#include <engine/queue/playlist.hpp>
#include <library/in_memory_repository.hpp>
#include <library/location.hpp>

#include <gtest/gtest.h>

namespace {

using wiola::gui::TrackSortDirection;
using wiola::gui::TrackSortField;
using wiola::gui::TrackSortKey;

TrackSortKey key(TrackSortField field, TrackSortDirection direction = TrackSortDirection::ascending)
{
    return {.field = field, .direction = direction, .tag_key = {}};
}

TEST(TrackSorter, AppliesMultipleKeysWithDeterministicIdentityFallback)
{
    wiola::engine::Playlist playlist;
    playlist.set({wiola::engine::Track::of("b.wav"), wiola::engine::Track::of("a.wav"),
        wiola::engine::Track::of("c.wav")});
    playlist.describe(playlist.tracks()[0].id(),
        {.title = "Same", .artist = "Zed", .album = {}, .duration = {}});
    playlist.describe(playlist.tracks()[1].id(),
        {.title = "Same", .artist = "Zed", .album = {}, .duration = {}});
    playlist.describe(playlist.tracks()[2].id(),
        {.title = "Before", .artist = "Amy", .album = {}, .duration = {}});
    wiola::library::InMemoryRepository repository;

    const auto sorted = wiola::gui::sorted_rows(wiola::gui::queue_rows(playlist.tracks()),
        &repository, {key(TrackSortField::artist), key(TrackSortField::title)});

    ASSERT_EQ(sorted.size(), 3);
    EXPECT_EQ(sorted[0].location, "c.wav");
    EXPECT_EQ(sorted[1].location, "a.wav");
    EXPECT_EQ(sorted[2].location, "b.wav");

    const auto descending =
        wiola::gui::sorted_rows(wiola::gui::queue_rows(playlist.tracks()), &repository,
            {key(TrackSortField::artist, TrackSortDirection::descending),
                key(TrackSortField::title, TrackSortDirection::descending)});
    EXPECT_EQ(descending[0].location, "a.wav");
    EXPECT_EQ(descending[1].location, "b.wav");
    EXPECT_EQ(descending[2].location, "c.wav");
}

TEST(TrackSorter, KeepsMissingDurationsLastAndUsesTagKeys)
{
    wiola::engine::Playlist playlist;
    playlist.set({wiola::engine::Track::of("unknown.wav"), wiola::engine::Track::of("short.wav"),
        wiola::engine::Track::of("long.wav")});
    playlist.describe(playlist.tracks()[1].id(),
        {.title = {}, .artist = {}, .album = {}, .duration = wiola::units::Time{1}});
    playlist.describe(playlist.tracks()[2].id(),
        {.title = {}, .artist = {}, .album = {}, .duration = wiola::units::Time{2}});
    wiola::library::InMemoryRepository repository;

    const auto ascending = wiola::gui::sorted_rows(wiola::gui::queue_rows(playlist.tracks()),
        &repository, {key(TrackSortField::duration)});
    const auto descending = wiola::gui::sorted_rows(wiola::gui::queue_rows(playlist.tracks()),
        &repository, {key(TrackSortField::duration, TrackSortDirection::descending)});

    EXPECT_EQ(ascending.back().location, "unknown.wav");
    EXPECT_EQ(descending.back().location, "unknown.wav");
    EXPECT_EQ(ascending.front().location, "short.wav");
    EXPECT_EQ(descending.front().location, "long.wav");

    wiola::library::TrackRecord record;
    record.id = wiola::library::media_id_for("unknown.wav");
    record.tags = {
        {.id = wiola::library::TagId{"tag"}, .key = "Mood", .value = "Calm"}
    };
    ASSERT_TRUE(repository.put_track(record));

    const auto tagged = wiola::gui::sorted_rows(wiola::gui::queue_rows(playlist.tracks()),
        &repository, {TrackSortKey::for_tag("Mood", TrackSortDirection::descending)});
    EXPECT_EQ(tagged.front().location, "unknown.wav");
}

TEST(TrackSorter, PreparesEachSortValueOncePerRowInsteadOfOncePerComparison)
{
    using namespace wiola;
    std::vector<gui::TrackRow> rows;
    for (int i = 0; i < 5000; ++i) {
        gui::TrackRow row;
        row.id = engine::QueueEntryId{static_cast<std::uint64_t>(i + 1)};
        row.location_key = std::to_string(i);
        row.metadata.title = "Track " + std::to_string(5000 - i);
        row.metadata.artist = "Artist " + std::to_string(i % 7);
        rows.push_back(std::move(row));
    }
    gui::TrackSortStatistics statistics;
    const auto sorted = gui::sorted_rows(rows, nullptr,
        {key(TrackSortField::artist), key(TrackSortField::title)}, &statistics);
    ASSERT_EQ(sorted.size(), rows.size());
    EXPECT_EQ(statistics.prepared_values, rows.size() * 4);
    EXPECT_GT(statistics.comparisons, rows.size());
    std::stop_source cancelled;
    cancelled.request_stop();
    EXPECT_TRUE(gui::sorted_rows(rows, nullptr, {key(TrackSortField::title)}, nullptr,
        cancelled.get_token())
            .empty());
}

} // namespace
