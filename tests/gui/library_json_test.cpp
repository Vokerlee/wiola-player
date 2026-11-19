/**
 * @file
 * @brief Schema, limits, migration defaults, and round-trip tests for library JSON.
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

#include <gui/library_json.hpp>

#include <QByteArray>

#include <gtest/gtest.h>

#include <string>

namespace {

using namespace wiola::library;
using namespace wiola::gui;

TrackRecord track_record()
{
    TrackRecord track;
    track.id = {.value = "/music/track.flac"};
    track.embedded = {.title = "Embedded",
        .artist = "Artist",
        .album = "Album",
        .duration = wiola::units::Time{1.5}};
    track.overrides.title = "My title";
    track.tags = {
        {.id = {.value = "tag-1"}, .key = "genre", .value = "Jazz"  },
        {.id = {.value = "tag-2"}, .key = "genre", .value = "Fusion"},
    };
    track.locations = {"/music/old.flac", "/music/track.flac"};
    track.fingerprint = "fingerprint-1";
    track.content_revision = 3;
    track.metadata_revision = 7;

    return track;
}

TEST(LibraryJson, RoundTripsLayeredRecordsAndDuplicatePlaylistOccurrences)
{
    LibrarySnapshot original{.tracks = {track_record()},
        .playlists = {},
        .workspaces = {WorkspaceRecord{.id = WorkspaceId{"workspace-1"},
            .root = "music",
            .recursive = false}}};
    PlaylistRecord playlist;
    playlist.id = {.value = "playlist-1"};
    playlist.name = "Favorites";
    playlist.entries = {
        {.id = {.value = "entry-1"}, .media = original.tracks.front().id},
        {.id = {.value = "entry-2"}, .media = original.tracks.front().id},
    };
    original.playlists.push_back(playlist);

    QByteArray json;
    ASSERT_EQ(write_library_json(original, json), LibraryJsonError::none);

    LibrarySnapshot restored;
    ASSERT_EQ(read_library_json(json, restored), LibraryJsonError::none);
    ASSERT_EQ(restored.tracks.size(), 1U);
    ASSERT_EQ(restored.playlists.size(), 1U);

    const TrackRecord& track = restored.tracks.front();
    EXPECT_EQ(track.id, original.tracks.front().id);
    EXPECT_EQ(track.embedded.title, "Embedded");
    EXPECT_EQ(track.display_title(), "My title");
    EXPECT_EQ(track.embedded.artist, "Artist");
    ASSERT_TRUE(track.embedded.duration.has_value());
    EXPECT_DOUBLE_EQ(track.embedded.duration->seconds(), 1.5);
    ASSERT_EQ(track.tags.size(), 2U);
    EXPECT_EQ(track.tags[1].value, "Fusion");
    EXPECT_EQ(track.locations, original.tracks.front().locations);
    EXPECT_EQ(track.fingerprint, "fingerprint-1");
    EXPECT_EQ(track.content_revision, 3U);
    EXPECT_EQ(track.metadata_revision, 7U);

    const PlaylistRecord& entries = restored.playlists.front();
    ASSERT_EQ(entries.entries.size(), 2U);
    EXPECT_EQ(entries.entries[0].media, entries.entries[1].media);
    EXPECT_NE(entries.entries[0].id, entries.entries[1].id);

    ASSERT_EQ(restored.workspaces.size(), 1U);
    EXPECT_EQ(restored.workspaces.front().id, WorkspaceId{"workspace-1"});
    EXPECT_EQ(restored.workspaces.front().root, original.workspaces.front().root);
    EXPECT_FALSE(restored.workspaces.front().recursive);
}

TEST(LibraryJson, SuppliesDefaultsForOptionalFieldsFromAnOlderRecordShape)
{
    const QByteArray json{R"({"version":1,"tracks":[{"id":"media-1"}],"playlists":[]})"};
    LibrarySnapshot restored;

    ASSERT_EQ(read_library_json(json, restored), LibraryJsonError::none);
    ASSERT_EQ(restored.tracks.size(), 1U);

    const TrackRecord& track = restored.tracks.front();
    EXPECT_EQ(track.id.value, "media-1");
    EXPECT_TRUE(track.embedded.title.empty());
    EXPECT_TRUE(track.tags.empty());
    EXPECT_TRUE(track.locations.empty());
    EXPECT_FALSE(track.overrides.title.has_value());
    EXPECT_EQ(track.content_revision, 0U);
    EXPECT_FALSE(track.missing);
}

TEST(LibraryJson, RejectsMalformedInputAndUnknownVersions)
{
    LibrarySnapshot restored;

    EXPECT_EQ(read_library_json("{", restored), LibraryJsonError::malformed);
    EXPECT_EQ(read_library_json("[]", restored), LibraryJsonError::malformed);
    EXPECT_EQ(read_library_json(R"({"version":1.5,"tracks":[],"playlists":[]})", restored),
        LibraryJsonError::malformed);

    const QByteArray future{R"({"version":2,"tracks":[],"playlists":[]})"};
    EXPECT_EQ(read_library_json(future, restored), LibraryJsonError::unsupported_version);
}

TEST(LibraryJson, RejectsDocumentsBeyondTheByteLimitBeforeParsing)
{
    const QByteArray oversized(16 * 1024 * 1024 + 1, ' ');
    LibrarySnapshot restored;

    EXPECT_EQ(read_library_json(oversized, restored), LibraryJsonError::too_large);
}

TEST(LibraryJson, RejectsDuplicateTrackIdentities)
{
    LibrarySnapshot library{
        .tracks = {track_record(), track_record()},
        .playlists = {},
        .workspaces = {}
    };
    QByteArray json;

    EXPECT_EQ(write_library_json(library, json), LibraryJsonError::invalid_record);
}

TEST(LibraryJson, RejectsMoreThan256TagsOnOneTrack)
{
    LibrarySnapshot library{.tracks = {track_record()}, .playlists = {}, .workspaces = {}};
    library.tracks.front().tags.clear();

    for (std::size_t index = 0; index < 257; ++index) {
        library.tracks.front().tags.push_back({.id = {.value = "tag-" + std::to_string(index)},
            .key = "index",
            .value = std::to_string(index)});
    }

    QByteArray json;
    EXPECT_EQ(write_library_json(library, json), LibraryJsonError::limit_exceeded);
}

TEST(LibraryJson, RejectsAPlaylistEntryForUnknownMedia)
{
    LibrarySnapshot library;
    PlaylistRecord playlist;
    playlist.id = {.value = "playlist-1"};
    playlist.name = "Broken";
    playlist.entries = {
        {.id = {.value = "entry-1"}, .media = {.value = "missing"}}
    };
    library.playlists.push_back(playlist);

    QByteArray json;
    EXPECT_EQ(write_library_json(library, json), LibraryJsonError::invalid_record);
}

TEST(LibraryJson, RejectsANonNumericRevision)
{
    const QByteArray json{
        R"({"version":1,"tracks":[{"id":"media-1","contentRevision":"latest"}],"playlists":[]})"};
    LibrarySnapshot restored;

    EXPECT_EQ(read_library_json(json, restored), LibraryJsonError::invalid_record);
}

} // namespace
