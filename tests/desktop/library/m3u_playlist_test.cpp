/**
 * @file
 * @brief M3U8 import/export regression tests.
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

#include <desktop/library/m3u_playlist.hpp>

#include <library/location.hpp>
#include <qt/paths.hpp>

#include <QCoreApplication>
#include <QTemporaryDir>

#include <gtest/gtest.h>

namespace {

class M3uPlaylist : public testing::Test {
protected:
    std::filesystem::path path(const char* name) const
    {
        return wiola::platform::qt::QtPathAdapter::from_qt(directory.filePath(name));
    }

    QTemporaryDir directory;
};

TEST_F(M3uPlaylist, RoundTripsUtf8RelativePathsAndDuplicateOccurrences)
{
    wiola::library::InMemoryRepository original;
    wiola::library::TrackRecord first;
    first.id = wiola::library::media_id_for(path("music/first.flac"));
    first.embedded.title = "First";
    first.embedded.artist = "Artist";
    first.embedded.duration = wiola::units::Time{12.5};
    first.locations = {path("music/first.flac")};

    wiola::library::TrackRecord second;
    second.id = wiola::library::media_id_for(path("music/é.flac"));
    second.locations = {path("music/é.flac")};
    ASSERT_TRUE(original.put_track(first));
    ASSERT_TRUE(original.put_track(second));

    const auto playlist = original.create_playlist("Favorites");
    ASSERT_TRUE(original.append_track(playlist, first.id));
    ASSERT_TRUE(original.append_track(playlist, second.id));
    ASSERT_TRUE(original.append_track(playlist, first.id));

    QByteArray encoded;
    ASSERT_EQ(wiola::app::M3uPlaylist::export_playlist(original, playlist, path(""), encoded),
        wiola::app::M3uError::none);
    EXPECT_TRUE(encoded.startsWith("#EXTM3U\n"));
    EXPECT_TRUE(encoded.contains("#EXTINF:12.500,Artist - First\n"));
    EXPECT_TRUE(encoded.contains("music/first.flac"));
    EXPECT_FALSE(encoded.contains(wiola::platform::qt::QtPathAdapter::to_qt(path("")).toUtf8()));

    wiola::library::InMemoryRepository imported;
    wiola::library::PlaylistId restored;
    ASSERT_EQ(wiola::app::M3uPlaylist::import_playlist(imported, path(""), "Imported", encoded,
                  restored),
        wiola::app::M3uError::none);

    const auto* saved = imported.playlist(restored);
    ASSERT_NE(saved, nullptr);
    ASSERT_EQ(saved->entries.size(), 3);
    EXPECT_EQ(saved->entries[0].media, first.id);
    EXPECT_EQ(saved->entries[1].media, second.id);
    EXPECT_EQ(saved->entries[2].media, first.id);
    EXPECT_EQ(imported.track(first.id)->embedded.artist, "Artist");
    EXPECT_TRUE(imported.track(first.id)->missing);
}

TEST_F(M3uPlaylist, AcceptsCrlfCommentsAndRetainsMissingEntries)
{
    wiola::library::InMemoryRepository repository;
    wiola::library::PlaylistId imported;

    ASSERT_EQ(wiola::app::M3uPlaylist::import_playlist(repository, path(""), "Missing",
                  "#EXTM3U\r\n# comment\r\n\r\nmissing.wav\r\n", imported),
        wiola::app::M3uError::none);

    const auto* saved = repository.playlist(imported);
    ASSERT_NE(saved, nullptr);
    ASSERT_EQ(saved->entries.size(), 1);
    const auto* track = repository.track(saved->entries.front().media);
    ASSERT_NE(track, nullptr);
    EXPECT_TRUE(track->missing);
    EXPECT_EQ(track->locations.front(), path("missing.wav"));
}

TEST_F(M3uPlaylist, RejectsInvalidUtf8AndMalformedExtinfWithoutPartialChanges)
{
    wiola::library::InMemoryRepository repository;
    wiola::library::PlaylistId imported;

    EXPECT_EQ(wiola::app::M3uPlaylist::import_playlist(repository, path(""), "Invalid",
                  QByteArray{"\xC3\x28", 2}, imported),
        wiola::app::M3uError::malformed);
    EXPECT_EQ(wiola::app::M3uPlaylist::import_playlist(repository, path(""), "Invalid",
                  "#EXTINF:not-a-duration,Title\nfile.wav\n", imported),
        wiola::app::M3uError::malformed);
    EXPECT_TRUE(repository.playlists().empty());
    EXPECT_TRUE(repository.tracks().empty());
}

TEST_F(M3uPlaylist, BoundsInputBeforeParsing)
{
    wiola::library::InMemoryRepository repository;
    wiola::library::PlaylistId imported;
    const QByteArray oversized(16 * 1024 * 1024 + 1, 'x');

    EXPECT_EQ(wiola::app::M3uPlaylist::import_playlist(repository, path(""), "Large", oversized,
                  imported),
        wiola::app::M3uError::too_large);
    EXPECT_TRUE(repository.playlists().empty());
}

} // namespace

int main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    QCoreApplication application{argc, argv};

    return RUN_ALL_TESTS();
}
