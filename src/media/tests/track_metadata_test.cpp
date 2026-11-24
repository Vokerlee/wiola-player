/**
 * @file
 * @brief Extended standard media metadata regression tests.
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

#include <wiola/media/metadata.hpp>

#include <gtest/gtest.h>

namespace {

TEST(TrackMetadata, ExistingReadersRemainCompatibleAndNewFieldsAffectEmptiness)
{
    const wiola::media::TrackMetadata original{.title = "Title",
        .artist = "Artist",
        .album = "Album",
        .duration = wiola::units::Time{1},
        .genre = {}};
    const wiola::media::TrackMetadata same{.title = "Title",
        .artist = "Artist",
        .album = "Album",
        .duration = wiola::units::Time{1},
        .genre = {}};
    EXPECT_EQ(original, same);

    wiola::media::TrackMetadata extended = original;
    extended.genre = "Jazz";
    extended.year = 2026;
    extended.comment = "Comment";
    extended.composer = "Composer";
    extended.track_number = 4;
    extended.disc_number = 2;
    EXPECT_NE(original, extended);

    wiola::media::Metadata metadata;
    EXPECT_TRUE(metadata.empty());
    metadata.track = extended;
    EXPECT_FALSE(metadata.empty());
}

TEST(TrackMetadata, EveryExtendedFieldIsIndependent)
{
    wiola::media::Metadata metadata;
    metadata.track.year = 2026;
    EXPECT_FALSE(metadata.empty());

    metadata = {};
    metadata.track.track_number = 1;
    EXPECT_FALSE(metadata.empty());

    metadata = {};
    metadata.track.disc_number = 1;
    EXPECT_FALSE(metadata.empty());

    metadata = {};
    metadata.track.comment = "Comment";
    EXPECT_FALSE(metadata.empty());

    metadata = {};
    metadata.track.composer = "Composer";
    EXPECT_FALSE(metadata.empty());
}

} // namespace
