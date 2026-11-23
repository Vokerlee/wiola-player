/**
 * @file
 * @brief Typed track sort key and value regression tests.
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

#include <gui/model/documents/queue_rows.hpp>

#include <gui/model/tracks/track_sort.hpp>

#include <engine/queue/playlist.hpp>
#include <library/in_memory_repository.hpp>
#include <library/location.hpp>

#include <gtest/gtest.h>

namespace {

using wiola::gui::TrackSortDirection;
using wiola::gui::TrackSortField;
using wiola::gui::TrackSortKey;

TrackSortKey standard(TrackSortField field,
    TrackSortDirection direction = TrackSortDirection::ascending)
{
    return {.field = field, .direction = direction, .tag_key = {}};
}

class TrackSort : public testing::Test {
protected:
    void SetUp() override
    {
        playlist.set({wiola::engine::Track::of("Music/Track.flac")});
        playlist.describe(playlist.tracks().front().id(),
            {.title = "  My Title",
                .artist = "Some Artist",
                .album = "An Album",
                .duration = wiola::units::Time{42.5}});

        record.id = wiola::library::media_id_for("Music/Track.flac");
        record.tags = {
            {.id = wiola::library::TagId{"tag-1"}, .key = "Genre", .value = "Jazz"  },
            {.id = wiola::library::TagId{"tag-2"}, .key = "Genre", .value = "Fusion"}
        };
        ASSERT_TRUE(repository.put_track(record));
    }

    wiola::gui::TrackRow track() const { return wiola::gui::queue_row(playlist.tracks().front()); }

    wiola::engine::Playlist playlist;
    wiola::library::InMemoryRepository repository;
    wiola::library::TrackRecord record;
};

TEST_F(TrackSort, ProducesTypedStandardValuesAndDirections)
{
    const auto title =
        wiola::gui::track_sort_value(standard(TrackSortField::title), track(), &repository);
    const auto artist =
        wiola::gui::track_sort_value(standard(TrackSortField::artist), track(), &repository);
    const auto album =
        wiola::gui::track_sort_value(standard(TrackSortField::album), track(), &repository);
    const auto duration =
        wiola::gui::track_sort_value(standard(TrackSortField::duration), track(), &repository);
    const auto path =
        wiola::gui::track_sort_value(standard(TrackSortField::path), track(), &repository);

    EXPECT_EQ(std::get<std::string>(title), "my title");
    EXPECT_EQ(std::get<std::string>(artist), "some artist");
    EXPECT_EQ(std::get<std::string>(album), "an album");
    EXPECT_EQ(std::get<wiola::units::Time>(duration), wiola::units::Time{42.5});
    EXPECT_EQ(std::get<std::string>(path), record.id.value);

    const TrackSortKey descending =
        standard(TrackSortField::duration, TrackSortDirection::descending);
    EXPECT_TRUE(descending.valid());
    EXPECT_EQ(descending.direction, TrackSortDirection::descending);
}

TEST_F(TrackSort, ResolvesCustomTagsWithoutCaseSensitivityAndKeepsMultipleValues)
{
    const TrackSortKey key = TrackSortKey::for_tag(" genre ", TrackSortDirection::descending);
    ASSERT_TRUE(key.valid());
    EXPECT_EQ(key.tag_key, "genre");

    const auto value = wiola::gui::track_sort_value(key, track(), &repository);
    EXPECT_EQ(std::get<std::string>(value), "jazz, fusion");

    const TrackSortKey missing = TrackSortKey::for_tag("Mood");
    EXPECT_EQ(std::get<std::string>(wiola::gui::track_sort_value(missing, track(), &repository)),
        "");
}

TEST_F(TrackSort, RejectsMalformedKeysAndRepresentsMissingDuration)
{
    EXPECT_FALSE(TrackSortKey::for_tag(" ").valid());
    EXPECT_FALSE((TrackSortKey{.field = TrackSortField::title, .tag_key = "unexpected"}.valid()));
    EXPECT_TRUE(std::holds_alternative<std::monostate>(
        wiola::gui::track_sort_value(TrackSortKey::for_tag(" "), track(), &repository)
    ));

    wiola::engine::Track unknown = wiola::engine::Track::of("unknown.wav");
    EXPECT_TRUE(std::holds_alternative<std::monostate>(wiola::gui::track_sort_value(
        standard(TrackSortField::duration), wiola::gui::queue_row(unknown), &repository
    )));
}

} // namespace
