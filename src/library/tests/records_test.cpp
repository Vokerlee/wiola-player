/**
 * @file
 * @brief Layering and identity tests for persistent library records.
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

#include <wiola/library/records.hpp>

#include <gtest/gtest.h>

namespace {

using namespace wiola::library;

TEST(TrackRecord, UserValuesOverrideTheDisplayWithoutReplacingEmbeddedMetadata)
{
    TrackRecord track;
    track.id = {.value = "/music/track.flac"};
    track.embedded = {.title = "Embedded title",
        .artist = "Embedded artist",
        .album = "Album",
        .duration = {}};

    EXPECT_EQ(track.display_title(), "Embedded title");
    EXPECT_EQ(track.display_artist(), "Embedded artist");

    track.overrides.title = "My title";
    track.overrides.artist = "My artist";

    EXPECT_EQ(track.display_title(), "My title");
    EXPECT_EQ(track.display_artist(), "My artist");
    EXPECT_EQ(track.display_album(), "Album");

    EXPECT_EQ(track.embedded.title, "Embedded title");
    EXPECT_EQ(track.embedded.artist, "Embedded artist");
}

TEST(TrackRecord, KeepsMultipleTagsAndLocationHistoryForOneMediaIdentity)
{
    TrackRecord track;
    track.id = {.value = "media-01"};
    track.tags = {
        {.id = {.value = "tag-01"}, .key = "genre", .value = "Jazz"  },
        {.id = {.value = "tag-02"}, .key = "genre", .value = "Fusion"},
    };
    track.locations = {"/music/old.flac", "/music/new.flac"};
    track.fingerprint = "fingerprint-01";
    track.content_revision = 3;
    track.metadata_revision = 7;
    track.missing = true;

    ASSERT_EQ(track.tags.size(), 2U);
    EXPECT_EQ(track.tags[0].key, track.tags[1].key);
    EXPECT_NE(track.tags[0].value, track.tags[1].value);
    EXPECT_NE(track.tags[0].id, track.tags[1].id);

    EXPECT_EQ(track.locations.size(), 2U);
    EXPECT_EQ(track.content_revision, 3U);
    EXPECT_EQ(track.metadata_revision, 7U);
    EXPECT_TRUE(track.missing);
}

TEST(PlaylistRecord, AllowsTwoEntriesForTheSameMediaWithoutSharingEntryIdentity)
{
    const MediaId media{.value = "media-01"};
    const PlaylistRecord playlist{
        .id = {.value = "playlist-01"},
        .name = "Favorites",
        .entries = {{.id = {.value = "entry-01"}, .media = media},
               {.id = {.value = "entry-02"}, .media = media}}
    };

    ASSERT_EQ(playlist.entries.size(), 2U);
    EXPECT_EQ(playlist.entries[0].media, playlist.entries[1].media);
    EXPECT_NE(playlist.entries[0].id, playlist.entries[1].id);
}

TEST(LibraryRecords, PreferredLocationUsesOnlyTheStoredOrder)
{
    wiola::library::TrackRecord record;
    EXPECT_TRUE(wiola::library::preferred_location(record).empty());
    record.locations = {"missing/preferred.flac", "other/track.wav"};
    EXPECT_EQ(wiola::library::preferred_location(record), record.locations.front());
    std::swap(record.locations.front(), record.locations.back());
    EXPECT_EQ(wiola::library::preferred_location(record), record.locations.front());
}

} // namespace
