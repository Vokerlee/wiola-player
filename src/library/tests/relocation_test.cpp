/**
 * @file
 * @brief Catalog relocation over supplied content facts.
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

#include <library/relocation.hpp>

#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace {

using namespace wiola::library;

TEST(MediaRelocation, MatchesOnlyOneUnambiguousStoredRecord)
{
    TrackRecord first;
    first.id = {.value = "media-1"};
    first.fingerprint = "observed-content";

    TrackRecord second;
    second.id = {.value = "media-2"};
    second.fingerprint = "observed-content";
    const std::array tracks{first, second};

    EXPECT_FALSE(match_relocation(tracks, "observed-content"));

    second.fingerprint = "different";
    const std::array distinct{first, second};
    const auto match = match_relocation(distinct, "observed-content");

    ASSERT_TRUE(match);
    EXPECT_EQ(*match, first.id);
}

TEST(MediaRelocation, EmptyAndUnknownObservationsNeverMatch)
{
    TrackRecord track;
    track.id = {.value = "media-1"};
    const std::array tracks{track};

    EXPECT_FALSE(match_relocation(tracks, ""));
    EXPECT_FALSE(match_relocation(tracks, "unknown"));
    EXPECT_FALSE(match_relocation({}, "observed-content"));
}

TEST(MediaRelocation, ReconcilesMovesAndEmbeddedEditsWithoutLosingUserMetadata)
{
    const std::filesystem::path original{"original.wav"};
    const std::filesystem::path moved{u8"музыка/moved.wav"};
    TrackRecord track;
    track.id = {.value = "media-1"};
    track.fingerprint = "original-content";
    track.locations = {original};
    track.missing = true;
    track.overrides.title = "My title";
    track.tags = {
        {.id = {.value = "tag-1"}, .key = "genre", .value = "Jazz"}
    };
    const auto identity = track.id;
    const auto overrides = track.overrides;
    const auto tags = track.tags;

    const auto move = reconcile_location(track, moved, "original-content");
    ASSERT_TRUE(move);
    EXPECT_EQ(move->media, identity);
    EXPECT_EQ(move->location, moved);
    EXPECT_EQ(move->kind, RelocationKind::unchanged);
    EXPECT_EQ(track.content_revision, 0U);
    EXPECT_EQ(track.locations, (std::vector{original, moved}));
    EXPECT_FALSE(track.missing);

    const auto edit = reconcile_location(track, moved, "embedded metadata changed");
    ASSERT_TRUE(edit);
    EXPECT_EQ(edit->kind, RelocationKind::edited);
    EXPECT_EQ(track.content_revision, 1U);
    EXPECT_EQ(track.locations, (std::vector{original, moved}));
    EXPECT_EQ(track.fingerprint, "embedded metadata changed");
    EXPECT_EQ(track.id, identity);
    EXPECT_EQ(track.overrides, overrides);
    EXPECT_EQ(track.tags, tags);
    EXPECT_FALSE(track.missing);
}

TEST(MediaRelocation, LearnsAnInitialFingerprintWithoutCallingItAnEdit)
{
    TrackRecord track;
    track.id = {.value = "media-1"};

    const auto learned = reconcile_location(track, "unavailable/track.wav", "observed-content");

    ASSERT_TRUE(learned);
    EXPECT_EQ(learned->kind, RelocationKind::learned);
    EXPECT_EQ(track.content_revision, 0U);
    EXPECT_EQ(track.fingerprint, "observed-content");
}

TEST(MediaRelocation, RepeatedObservationDoesNotAddLocationsOrAdvanceRevisions)
{
    TrackRecord track;
    track.id = {.value = "media-1"};
    track.fingerprint = "observed-content";
    track.content_revision = 7;
    track.metadata_revision = 11;
    track.locations = {"missing/track.wav"};
    const auto before = track;

    const auto unchanged = reconcile_location(track, track.locations.front(), "observed-content");

    ASSERT_TRUE(unchanged);
    EXPECT_EQ(unchanged->kind, RelocationKind::unchanged);
    EXPECT_EQ(track, before);
}

TEST(MediaRelocation, InvalidIdentityOrMissingFactsLeaveTheRecordUnchanged)
{
    TrackRecord track;
    track.fingerprint = "known";
    track.missing = true;
    const auto invalid = track;

    EXPECT_FALSE(reconcile_location(track, "track.wav", "observed-content"));
    EXPECT_EQ(track, invalid);

    track.id = {.value = "media-1"};
    const auto missing = track;
    EXPECT_FALSE(reconcile_location(track, "track.wav", ""));
    EXPECT_EQ(track, missing);
}

} // namespace
