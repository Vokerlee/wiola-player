/**
 * @file
 * @brief Learned metadata merges preserve user-owned record layers.
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

#include <wiola/app/metadata/metadata_merge.hpp>

#include <gtest/gtest.h>

namespace {

TEST(MetadataMerge, LearnsOnlyChangedFieldsWithoutTouchingUserLayers)
{
    wiola::library::InMemoryRepository repository;
    wiola::library::TrackRecord record;
    record.id = wiola::library::MediaId{"media"};
    record.embedded.title = "Old title";
    record.embedded.artist = "Known artist";
    record.embedded.album = "Old album";
    record.overrides.title = "User title";
    record.metadata_revision = 7;
    record.tags = {
        {.id = wiola::library::TagId{"tag"}, .key = "genre", .value = "Jazz"}
    };
    ASSERT_TRUE(repository.put_track(record));

    wiola::media::Metadata learned;
    learned.track.title = "Learned title";
    learned.track.album = "Learned album";
    learned.track.duration = wiola::units::Time{42};
    learned.track.genre = "Jazz";
    learned.track.year = 2024;
    learned.track.comment = "Live";
    learned.track.composer = "Composer";
    learned.track.track_number = 3;
    learned.track.disc_number = 2;

    wiola::app::MetadataMerge merge;

    ASSERT_TRUE(merge.merge(repository, record.id, learned));

    const auto* updated = repository.track(record.id);

    ASSERT_NE(updated, nullptr);
    EXPECT_EQ(updated->embedded.title, "Learned title");
    EXPECT_EQ(updated->embedded.artist, "Known artist");
    EXPECT_EQ(updated->embedded.album, "Learned album");
    EXPECT_EQ(updated->embedded.duration, wiola::units::Time{42});
    EXPECT_EQ(updated->embedded.genre, "Jazz");
    EXPECT_EQ(updated->embedded.year, 2024);
    EXPECT_EQ(updated->embedded.comment, "Live");
    EXPECT_EQ(updated->embedded.composer, "Composer");
    EXPECT_EQ(updated->embedded.track_number, 3);
    EXPECT_EQ(updated->embedded.disc_number, 2);
    EXPECT_EQ(updated->display_title(), "User title");
    EXPECT_EQ(updated->tags.size(), 1);
    EXPECT_EQ(updated->metadata_revision, 8);

    EXPECT_FALSE(merge.merge(repository, record.id, learned));
    EXPECT_EQ(repository.track(record.id)->metadata_revision, 8);

    const wiola::media::Metadata unavailable_fields;

    EXPECT_FALSE(merge.merge(repository, record.id, unavailable_fields));
    EXPECT_EQ(repository.track(record.id)->embedded.duration, learned.track.duration);
    EXPECT_EQ(repository.track(record.id)->embedded.year, learned.track.year);
    EXPECT_FALSE(merge.merge(repository, wiola::library::MediaId{"missing"}, learned));
}

} // namespace
