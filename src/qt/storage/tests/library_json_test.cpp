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

#include <qt/storage/library_json.hpp>
#include <wiola/library/metadata_patch.hpp>

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <gtest/gtest.h>

#include <string>

namespace {

using namespace wiola::library;
using namespace wiola::persistence::qt;

TEST(LibraryJson, MetadataPatchSemanticsKeepTheExistingStoredSchema)
{
    wiola::library::LibrarySnapshot original;
    TrackRecord track;
    track.id = MediaId{"media"};
    track.embedded.title = "Embedded";
    track.embedded.year = 2020;

    StandardMetadataPatch clear;
    clear.title = FieldEdit<std::string>::clear();
    clear.year = FieldEdit<std::uint32_t>::clear();
    ASSERT_EQ(apply_metadata_patch(track.overrides, clear), MetadataPatchResult::changed);

    original.tracks.push_back(track);

    QByteArray encoded;

    ASSERT_EQ(write_library_json(original, encoded), LibraryJsonError::none);

    wiola::library::LibrarySnapshot restored;

    ASSERT_EQ(read_library_json(encoded, restored), LibraryJsonError::none);
    ASSERT_EQ(restored.tracks.size(), 1U);
    EXPECT_EQ(restored.tracks.front(), track);

    StandardMetadataPatch inherit;
    inherit.title = FieldEdit<std::string>::inherit();
    inherit.year = FieldEdit<std::uint32_t>::inherit();
    ASSERT_EQ(apply_metadata_patch(restored.tracks.front().overrides, inherit),
        MetadataPatchResult::changed);
    ASSERT_EQ(write_library_json(restored, encoded), LibraryJsonError::none);
    ASSERT_EQ(read_library_json(encoded, original), LibraryJsonError::none);
    EXPECT_FALSE(original.tracks.front().overrides.title.has_value());
    EXPECT_FALSE(original.tracks.front().overrides.year.has_value());
    EXPECT_EQ(original.tracks.front().embedded, track.embedded);
}

TrackRecord track_record()
{
    TrackRecord track;
    track.id = {.value = "/music/track.flac"};
    track.embedded = {.title = "Embedded",
        .artist = "Artist",
        .album = "Album",
        .duration = wiola::units::Time{1.5},
        .genre = "Rock",
        .year = 2020,
        .comment = "Embedded comment",
        .composer = "Embedded composer",
        .track_number = 1,
        .disc_number = 1};
    track.overrides.title = "My title";
    track.overrides.genre = "Jazz";
    track.overrides.comment = "User comment";
    track.overrides.composer = "User composer";
    track.overrides.year = 2026;
    track.overrides.track_number = std::optional<std::uint32_t>{};
    track.overrides.disc_number = 3;
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
    wiola::library::LibrarySnapshot original{.tracks = {track_record()},
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

    wiola::library::LibrarySnapshot restored;

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
    EXPECT_EQ(track.embedded.genre, "Rock");
    EXPECT_EQ(track.embedded.year, 2020);
    EXPECT_EQ(track.embedded.comment, "Embedded comment");
    EXPECT_EQ(track.embedded.composer, "Embedded composer");
    EXPECT_EQ(track.embedded.track_number, 1);
    EXPECT_EQ(track.embedded.disc_number, 1);
    EXPECT_EQ(track.overrides.genre, "Jazz");
    EXPECT_EQ(track.overrides.comment, "User comment");
    EXPECT_EQ(track.overrides.composer, "User composer");
    EXPECT_EQ(track.overrides.year, 2026);
    ASSERT_TRUE(track.overrides.track_number.has_value());
    EXPECT_FALSE(track.overrides.track_number->has_value());
    EXPECT_EQ(track.overrides.disc_number, 3);
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
    wiola::library::LibrarySnapshot restored;

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
    wiola::library::LibrarySnapshot restored;

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
    wiola::library::LibrarySnapshot restored;

    EXPECT_EQ(read_library_json(oversized, restored), LibraryJsonError::too_large);
}

TEST(LibraryJson, RejectsDuplicateTrackIdentities)
{
    wiola::library::LibrarySnapshot library{
        .tracks = {track_record(), track_record()},
        .playlists = {},
        .workspaces = {}
    };
    QByteArray json;

    EXPECT_EQ(write_library_json(library, json), LibraryJsonError::invalid_record);
}

TEST(LibraryJson, RejectsMoreThan256TagsOnOneTrack)
{
    wiola::library::LibrarySnapshot library{.tracks = {track_record()},
        .playlists = {},
        .workspaces = {}};
    library.tracks.front().tags.clear();

    for (std::size_t index = 0; index < 257; ++index) {
        library.tracks.front().tags.push_back({.id = {.value = "tag-" + std::to_string(index)},
            .key = "index",
            .value = std::to_string(index)});
    }

    QByteArray json;

    EXPECT_EQ(write_library_json(library, json), LibraryJsonError::limit_exceeded);
}

TEST(LibraryJson, RejectsNormalizedDuplicateTagsAndTrimsRestoredText)
{
    const QByteArray duplicates{
        R"({"version":1,"tracks":[{"id":"media-1","tags":[{"id":"tag-1","key":"Genre","value":"Jazz"},{"id":"tag-2","key":"genre","value":"jazz"}]}],"playlists":[]})"};
    wiola::library::LibrarySnapshot restored;

    EXPECT_EQ(read_library_json(duplicates, restored), LibraryJsonError::invalid_record);

    const QByteArray spaced{
        R"({"version":1,"tracks":[{"id":"media-1","tags":[{"id":"tag-1","key":" Genre ","value":" Nu Jazz "}]}],"playlists":[]})"};

    ASSERT_EQ(read_library_json(spaced, restored), LibraryJsonError::none);
    ASSERT_EQ(restored.tracks.size(), 1);
    ASSERT_EQ(restored.tracks.front().tags.size(), 1);
    EXPECT_EQ(restored.tracks.front().tags.front().key, "Genre");
    EXPECT_EQ(restored.tracks.front().tags.front().value, "Nu Jazz");
}

TEST(LibraryJson, RejectsAPlaylistEntryForUnknownMedia)
{
    wiola::library::LibrarySnapshot library;
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
    wiola::library::LibrarySnapshot restored;

    EXPECT_EQ(read_library_json(json, restored), LibraryJsonError::invalid_record);
}

TEST(LibraryJson, MalformedRecordFieldsCannotPartiallyReplaceTheStoredLibrary)
{
    LibrarySnapshot original;
    original.tracks = {track_record()};
    QByteArray encoded;
    ASSERT_EQ(write_library_json(original, encoded), LibraryJsonError::none);
    const auto root = QJsonDocument::fromJson(encoded).object();
    const auto track = root["tracks"].toArray().first().toObject();
    const auto reject = [&](QJsonObject candidate) {
        auto output = original;
        EXPECT_NE(read_library_json(QJsonDocument{candidate}.toJson(), output),
            LibraryJsonError::none);
        EXPECT_EQ(output.tracks, original.tracks);
        EXPECT_EQ(output.playlists, original.playlists);
        EXPECT_EQ(output.workspaces, original.workspaces);
    };
    for (const auto& [key, value] : std::vector<std::pair<QString, QJsonValue>>{
             {"id", false},
             {"id", QString(4097, 'a')},
             {"embedded", false},
             {"overrides", false},
             {"tags", false},
             {"tags", QJsonArray{false}},
             {"tags", QJsonArray{QJsonObject{{"id", ""}, {"key", "Mood"}, {"value", "Calm"}}}},
             {"locations", false},
             {"locations", QJsonArray{false}},
             {"missing", "false"},
             {"contentRevision", false}
    }) {
        SCOPED_TRACE(key.toStdString());
        auto changed = track;
        changed[key] = value;
        auto candidate = root;
        candidate["tracks"] = QJsonArray{changed};
        reject(candidate);
    }
    for (const auto& [key, value] : std::vector<std::pair<QString, QJsonValue>>{
             {"title",           false       },
             {"year",            -1          },
             {"year",            4294967296.0},
             {"durationSeconds", -1          }
    }) {
        SCOPED_TRACE(key.toStdString());
        auto embedded = track["embedded"].toObject();
        embedded[key] = value;
        auto changed = track;
        changed["embedded"] = embedded;
        auto candidate = root;
        candidate["tracks"] = QJsonArray{changed};
        reject(candidate);
    }
    for (const auto& [key, value] : std::vector<std::pair<QString, QJsonValue>>{
             {"playlists",  QJsonArray{false}                                               },
             {"playlists",  QJsonArray{QJsonObject{{"id", "saved"}, {"name", false}}}       },
             {"workspaces", false                                                           },
             {"workspaces", QJsonArray{false}                                               },
             {"workspaces", QJsonArray{QJsonObject{{"id", "folder"}, {"root", ""}}}         },
             {"workspaces",
              QJsonArray{
                     QJsonObject{{"id", "folder"}, {"root", "music"}, {"recursive", "yes"}}}}
    }) {
        auto candidate = root;
        candidate[key] = value;
        reject(candidate);
    }
}

} // namespace
