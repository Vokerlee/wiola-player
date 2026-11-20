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

#include <gui/track_sorter.hpp>

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

    const auto sorted = wiola::gui::sorted_tracks(playlist.tracks(), repository,
        {key(TrackSortField::artist), key(TrackSortField::title)});

    ASSERT_EQ(sorted.size(), 3);
    EXPECT_EQ(sorted[0].path(), "c.wav");
    EXPECT_EQ(sorted[1].path(), "a.wav");
    EXPECT_EQ(sorted[2].path(), "b.wav");

    const auto descending = wiola::gui::sorted_tracks(playlist.tracks(), repository,
        {key(TrackSortField::artist, TrackSortDirection::descending),
            key(TrackSortField::title, TrackSortDirection::descending)});
    EXPECT_EQ(descending[0].path(), "a.wav");
    EXPECT_EQ(descending[1].path(), "b.wav");
    EXPECT_EQ(descending[2].path(), "c.wav");
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

    const auto ascending =
        wiola::gui::sorted_tracks(playlist.tracks(), repository, {key(TrackSortField::duration)});
    const auto descending = wiola::gui::sorted_tracks(playlist.tracks(), repository,
        {key(TrackSortField::duration, TrackSortDirection::descending)});

    EXPECT_EQ(ascending.back().path(), "unknown.wav");
    EXPECT_EQ(descending.back().path(), "unknown.wav");
    EXPECT_EQ(ascending.front().path(), "short.wav");
    EXPECT_EQ(descending.front().path(), "long.wav");

    wiola::library::TrackRecord record;
    record.id = wiola::library::media_id_for("unknown.wav");
    record.tags = {
        {.id = wiola::library::TagId{"tag"}, .key = "Mood", .value = "Calm"}
    };
    ASSERT_TRUE(repository.put_track(record));

    const auto tagged = wiola::gui::sorted_tracks(playlist.tracks(), repository,
        {TrackSortKey::for_tag("Mood", TrackSortDirection::descending)});
    EXPECT_EQ(tagged.front().path(), "unknown.wav");
}

} // namespace
