/**
 * @file
 * @brief Saved playlist occurrence identity and shared metadata update contracts.
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

#include <wiola/library/in_memory_repository.hpp>
#include <wiola/presentation/documents/saved_playlist_rows.hpp>

#include <gtest/gtest.h>

namespace {

using namespace wiola;

TEST(SavedPlaylistRows, PreservesDuplicateOccurrencesWhileUpdatingSharedMetadata)
{
    library::InMemoryRepository repository;
    library::TrackRecord track;
    track.id = library::MediaId{"media"};
    track.embedded.title = "Original";
    track.locations = {"song.flac"};
    ASSERT_TRUE(repository.put_track(track));
    const auto playlist = repository.create_playlist("Playlist");
    const auto first = repository.append_track(playlist, track.id);
    const auto second = repository.append_track(playlist, track.id);
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);

    gui::SavedPlaylistRows rows;
    EXPECT_FALSE(rows.reconcile(repository, library::PlaylistId{"absent"}));
    ASSERT_TRUE(rows.reconcile(repository, playlist));
    ASSERT_EQ(rows.rows().size(), 2);
    EXPECT_EQ(rows.rows()[0].id, gui::TrackRowId{*first});
    EXPECT_EQ(rows.rows()[1].id, gui::TrackRowId{*second});
    EXPECT_EQ(rows.media_rows(track.id), rows.rows());
    EXPECT_TRUE(rows.contains(track.id));
    EXPECT_NE(rows.row(*first), nullptr);
    EXPECT_EQ(rows.row(library::PlaylistEntryId{"absent"}), nullptr);
    EXPECT_FALSE(rows.update_media(repository, track.id));
    library::InMemoryRepository unavailable;
    EXPECT_FALSE(rows.update_media(unavailable, track.id));
    EXPECT_FALSE(rows.update_media(repository, library::MediaId{"absent"}));
    EXPECT_TRUE(rows.media_rows(library::MediaId{"absent"}).empty());

    const auto previous = rows.rows();
    track.overrides.title = "Updated";
    ASSERT_TRUE(repository.put_track(track));
    EXPECT_TRUE(rows.update_media(repository, track.id));
    EXPECT_FALSE(rows.update_media(repository, track.id));
    EXPECT_EQ(rows.row(*first)->metadata.title, "Updated");
    EXPECT_EQ(rows.row(*second)->metadata.title, "Updated");
    EXPECT_EQ(previous.front().metadata.title, "Original");

    ASSERT_TRUE(repository.remove_entry(playlist, *first));
    ASSERT_TRUE(rows.reconcile(repository, playlist));
    ASSERT_EQ(rows.rows().size(), 1);
    EXPECT_EQ(rows.row(*first), nullptr);
    EXPECT_NE(rows.row(*second), nullptr);

    EXPECT_FALSE(rows.reconcile(repository, library::PlaylistId{"absent"}));
    EXPECT_EQ(rows.rows().size(), 1);

    ASSERT_TRUE(repository.remove_entry(playlist, *second));
    ASSERT_TRUE(rows.reconcile(repository, playlist));
    EXPECT_TRUE(rows.rows().empty());
    EXPECT_FALSE(rows.contains(track.id));
}

} // namespace
