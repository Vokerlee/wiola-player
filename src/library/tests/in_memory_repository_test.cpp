/**
 * @file
 * @brief Deterministic contract tests for in-memory library storage.
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

#include <gtest/gtest.h>

#include <string>

namespace {

using namespace wiola::library;
using Repository = wiola::library::InMemoryRepository;

class InMemoryRepository : public testing::Test {
protected:
    void SetUp() override
    {
        TrackRecord track;
        track.id = media;

        ASSERT_TRUE(repository.put_track(track));
    }

    MediaId media{.value = "media-01"};
    Repository repository;
};

TEST_F(InMemoryRepository, RejectsAnUnidentifiedTrack)
{
    EXPECT_FALSE(repository.put_track(TrackRecord{}));
    EXPECT_EQ(repository.track({}), nullptr);
}

TEST_F(InMemoryRepository, ReturnsTracksInIdentityOrder)
{
    TrackRecord later;
    later.id = {.value = "media-02"};
    ASSERT_TRUE(repository.put_track(later));

    TrackRecord earlier;
    earlier.id = {.value = "media-00"};
    ASSERT_TRUE(repository.put_track(earlier));

    const auto tracks = repository.tracks();

    ASSERT_EQ(tracks.size(), 3U);
    EXPECT_EQ(tracks[0].id.value, "media-00");
    EXPECT_EQ(tracks[1].id.value, "media-01");
    EXPECT_EQ(tracks[2].id.value, "media-02");
}

TEST_F(InMemoryRepository, CreatesDeterministicPlaylistIdentitiesAndAvoidsRestoredOnes)
{
    const PlaylistId first = repository.create_playlist("Favorites");
    const PlaylistId second = repository.create_playlist("Recent");

    EXPECT_EQ(first.value, "playlist-1");
    EXPECT_EQ(second.value, "playlist-2");

    PlaylistRecord restored{.id = {.value = "playlist-3"}, .name = "Restored", .entries = {}};

    ASSERT_TRUE(repository.put_playlist(restored));

    EXPECT_EQ(repository.create_playlist("Next").value, "playlist-4");
    EXPECT_EQ(repository.playlists().size(), 4U);

    EXPECT_TRUE(repository.rename_playlist(first, "Morning"));
    ASSERT_NE(repository.playlist(first), nullptr);
    EXPECT_EQ(repository.playlist(first)->name, "Morning");
    EXPECT_FALSE(repository.rename_playlist(PlaylistId{.value = "missing"}, "Nothing"));
}

TEST_F(InMemoryRepository, AllowsDuplicateMediaAsDistinctPlaylistEntries)
{
    const PlaylistId playlist = repository.create_playlist("Favorites");

    const auto first = repository.append_track(playlist, media);
    const auto second = repository.append_track(playlist, media);

    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_NE(*first, *second);
    EXPECT_EQ(first->value, "entry-1");
    EXPECT_EQ(second->value, "entry-2");

    ASSERT_NE(repository.playlist(playlist), nullptr);
    ASSERT_EQ(repository.playlist(playlist)->entries.size(), 2U);
    EXPECT_EQ(repository.playlist(playlist)->entries[0].media, media);
    EXPECT_EQ(repository.playlist(playlist)->entries[1].media, media);

    EXPECT_TRUE(repository.remove_entry(playlist, *first));
    ASSERT_EQ(repository.playlist(playlist)->entries.size(), 1U);
    EXPECT_EQ(repository.playlist(playlist)->entries.front().id, *second);
    EXPECT_FALSE(repository.remove_entry(playlist, *first));
}

TEST_F(InMemoryRepository, RejectsEntriesWhoseMediaIsNotInTheLibrary)
{
    const PlaylistId playlist = repository.create_playlist("Favorites");

    EXPECT_FALSE(repository.append_track(playlist, MediaId{.value = "missing"}).has_value());
    EXPECT_FALSE(repository.append_track(PlaylistId{.value = "missing"}, media).has_value());

    PlaylistRecord invalid{.id = playlist,
        .name = "Invalid",
        .entries = {{.id = {.value = "entry-01"}, .media = {.value = "missing"}}}};

    EXPECT_FALSE(repository.put_playlist(invalid));
}

TEST_F(InMemoryRepository, RejectsDuplicateEntryIdsWithinAStoredPlaylist)
{
    PlaylistRecord invalid{
        .id = {.value = "playlist-01"},
        .name = "Invalid",
        .entries = {{.id = {.value = "entry-01"}, .media = media},
               {.id = {.value = "entry-01"}, .media = media}}
    };

    EXPECT_FALSE(repository.put_playlist(invalid));
}

TEST_F(InMemoryRepository, AllowsMultipleTagValuesButRejectsAnExactDuplicate)
{
    const TagId jazz = repository.add_tag(media, "genre", "Jazz");
    const TagId fusion = repository.add_tag(media, "genre", "Fusion");

    EXPECT_TRUE(jazz.valid());
    EXPECT_TRUE(fusion.valid());
    EXPECT_NE(jazz, fusion);
    EXPECT_FALSE(repository.add_tag(media, "genre", "Jazz").valid());

    ASSERT_NE(repository.track(media), nullptr);
    EXPECT_EQ(repository.track(media)->tags.size(), 2U);
    EXPECT_EQ(repository.track(media)->metadata_revision, 2U);

    EXPECT_TRUE(repository.remove_tag(media, jazz));
    EXPECT_FALSE(repository.remove_tag(media, jazz));
    EXPECT_EQ(repository.track(media)->tags.size(), 1U);
    EXPECT_EQ(repository.track(media)->metadata_revision, 3U);
}

TEST_F(InMemoryRepository, MatchesTagsWithoutCaseOrOuterWhitespaceButKeepsOriginalText)
{
    const TagId jazz = repository.add_tag(media, "  Genre ", "  Nu Jazz  ");

    ASSERT_TRUE(jazz.valid());
    EXPECT_EQ(repository.track(media)->tags.front().key, "Genre");
    EXPECT_EQ(repository.track(media)->tags.front().value, "Nu Jazz");
    EXPECT_FALSE(repository.add_tag(media, "genre", "nu jazz").valid());
    EXPECT_FALSE(repository.add_tag(media, "GENRE", "NU JAZZ").valid());
    EXPECT_TRUE(repository.add_tag(media, "genre", "Jazz").valid());
    EXPECT_FALSE(repository.add_tag(media, " ", "Jazz").valid());
    EXPECT_FALSE(repository.add_tag(media, "genre", " ").valid());
}

TEST_F(InMemoryRepository, RejectsInvalidOrNormalizedDuplicateRestoredTags)
{
    TrackRecord restored;
    restored.id = {.value = "media-restored"};
    restored.tags = {
        {.id = {.value = "tag-1"}, .key = "Genre", .value = "Jazz"},
        {.id = {.value = "tag-2"}, .key = "genre", .value = "jazz"}
    };

    EXPECT_FALSE(repository.put_track(restored));
    EXPECT_EQ(repository.track(restored.id), nullptr);

    restored.tags.pop_back();
    restored.tags.front().key = " Genre ";
    EXPECT_FALSE(repository.put_track(restored));

    restored.tags.front().key = "Genre";
    EXPECT_TRUE(repository.put_track(restored));
}

TEST_F(InMemoryRepository, RejectsDuplicateTagIdentitiesWithoutReplacingExistingState)
{
    TrackRecord restored;
    restored.id = {"media-duplicate-tag-id"};
    restored.embedded.title = "Retained";
    ASSERT_TRUE(repository.put_track(restored));

    restored.embedded.title = "Rejected";
    restored.tags = {
        {.id = {"tag-same"}, .key = "genre", .value = "Jazz"  },
        {.id = {"tag-same"}, .key = "genre", .value = "Fusion"}
    };
    EXPECT_FALSE(repository.put_track(restored));
    ASSERT_NE(repository.track(restored.id), nullptr);
    EXPECT_EQ(repository.track(restored.id)->embedded.title, "Retained");
    EXPECT_TRUE(repository.track(restored.id)->tags.empty());
}

TEST_F(InMemoryRepository, BoundsTagsAndAvoidsARestoredTagIdentity)
{
    TrackRecord restored;
    restored.id = {.value = "media-restored"};
    restored.tags = {
        {.id = {.value = "tag-1"}, .key = "genre", .value = "Jazz"}
    };
    ASSERT_TRUE(repository.put_track(restored));

    const MediaId id = restored.id;
    const TagId next = repository.add_tag(id, "genre", "Fusion");

    EXPECT_EQ(next.value, "tag-2");

    for (std::size_t index = 0; index < 254; ++index)
        ASSERT_TRUE(repository.add_tag(id, "index", std::to_string(index)).valid());

    ASSERT_EQ(repository.track(id)->tags.size(), 256U);
    EXPECT_FALSE(repository.add_tag(id, "overflow", "no").valid());
}

TEST_F(InMemoryRepository, StoresObservedWorkspaceRootsAndPersistentIdentities)
{
    const WorkspaceId first = repository.create_workspace("music/album/..", false);

    ASSERT_TRUE(first.valid());
    ASSERT_NE(repository.workspace(first), nullptr);

    EXPECT_EQ(repository.workspace(first)->root,
        std::filesystem::path{"music/album/.."}.lexically_normal());
    EXPECT_FALSE(repository.workspace(first)->recursive);

    WorkspaceRecord restored{.id = WorkspaceId{"workspace-restored"},
        .root = "other",
        .recursive = true};

    ASSERT_TRUE(repository.put_workspace(restored));
    EXPECT_EQ(repository.workspace(restored.id)->root, restored.root);
    EXPECT_EQ(repository.create_workspace("next").value, "workspace-2");

    EXPECT_FALSE(repository.create_workspace({}).valid());
    EXPECT_TRUE(repository.delete_workspace(restored.id));
    EXPECT_FALSE(repository.delete_workspace(restored.id));
    EXPECT_EQ(repository.workspace(restored.id), nullptr);
}

} // namespace
