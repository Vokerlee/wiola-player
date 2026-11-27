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

#include <array>
#include <cstddef>
#include <utility>

namespace {

TEST(TrackMetadata, ExistingReadersRemainCompatibleAndNewFieldsAffectEmptiness)
{
    wiola::media::TrackMetadata original;
    original.title = "Title";
    original.artist = "Artist";
    original.album = "Album";
    original.duration = wiola::units::Time{1};
    const auto same = original;

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

TEST(TrackMetadata, EveryTextFieldIndependentlyChangesEmptinessAndEquality)
{
    using wiola::media::TrackMetadata;
    const TrackMetadata empty;

    for (const auto& [name, member] : std::array{
             std::pair{"title",    &TrackMetadata::title   },
             std::pair{"artist",   &TrackMetadata::artist  },
             std::pair{"album",    &TrackMetadata::album   },
             std::pair{"genre",    &TrackMetadata::genre   },
             std::pair{"comment",  &TrackMetadata::comment },
             std::pair{"composer", &TrackMetadata::composer}
    }) {
        SCOPED_TRACE(name);
        wiola::media::Metadata metadata;

        metadata.track.*member = "value";

        EXPECT_FALSE(metadata.empty());
        EXPECT_NE(metadata.track, empty);
        EXPECT_NE(empty, metadata.track);

        metadata.track.*member = {};

        EXPECT_TRUE(metadata.empty());
        EXPECT_EQ(metadata.track, empty);
    }
}

TEST(TrackMetadata, EveryNumberFieldDistinguishesAnExplicitZeroFromAnAbsentValue)
{
    using wiola::media::TrackMetadata;
    const TrackMetadata empty;

    for (const auto& [name, member] : std::array{
             std::pair{"year",         &TrackMetadata::year        },
             std::pair{"track number", &TrackMetadata::track_number},
             std::pair{"disc number",  &TrackMetadata::disc_number }
    }) {
        SCOPED_TRACE(name);
        wiola::media::Metadata metadata;

        metadata.track.*member = 0;

        EXPECT_FALSE(metadata.empty());
        EXPECT_NE(metadata.track, empty);
        EXPECT_NE(empty, metadata.track);

        (metadata.track.*member).reset();

        EXPECT_TRUE(metadata.empty());
        EXPECT_EQ(metadata.track, empty);
    }
}

TEST(TrackMetadata, ZeroDurationIsPresentAndDistinctFromAnUnknownDuration)
{
    wiola::media::Metadata metadata;
    const wiola::media::TrackMetadata empty;
    metadata.track.duration = wiola::units::Time{};

    EXPECT_FALSE(metadata.empty());
    EXPECT_NE(metadata.track, empty);
    EXPECT_NE(empty, metadata.track);

    metadata.track.duration.reset();

    EXPECT_TRUE(metadata.empty());
    EXPECT_EQ(metadata.track, empty);
}

TEST(TrackMetadata, ArtworkBytesAloneMakeMetadataNonempty)
{
    wiola::media::Metadata metadata;
    metadata.artwork.mime_type = "image/png";
    metadata.artwork.content_id = 123;

    EXPECT_TRUE(metadata.empty());
    EXPECT_TRUE(metadata.artwork.empty());

    metadata.artwork.data = {std::byte{0x61}};

    EXPECT_FALSE(metadata.empty());
    EXPECT_FALSE(metadata.artwork.empty());

    metadata.artwork.data.clear();

    EXPECT_TRUE(metadata.empty());
}

} // namespace
