/**
 * @file
 * @brief Catalog restore validation, identity collision, and rollback contracts.
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

#include <gtest/gtest.h>
#include <wiola/library/repository_transaction.hpp>
#include <wiola/library/snapshot.hpp>
#include <wiola/library/tags.hpp>

namespace {

using namespace wiola::library;

class RepositoryRestore : public testing::Test {
protected:
    void SetUp() override
    {
        TrackRecord track;
        track.id = media;
        track.embedded.title = "Retained";
        ASSERT_TRUE(repository.put_track(track));
        playlist = repository.create_playlist("Saved");
        ASSERT_TRUE(repository.append_track(playlist, media));
        workspace = repository.create_workspace("observed");
        tag = repository.add_tag(media, "Mood", "Calm");
        ASSERT_TRUE(tag.valid());
        original = snapshot_of(repository);
    }

    void expect_original() const
    {
        EXPECT_EQ(repository.tracks(), original.tracks);
        EXPECT_EQ(repository.playlists(), original.playlists);
        EXPECT_EQ(repository.workspaces(), original.workspaces);
        EXPECT_EQ(repository.change_revision(), 0U);
        EXPECT_FALSE(repository.staged_delta());
    }

    InMemoryRepository repository;
    const MediaId media{"media"};
    PlaylistId playlist;
    WorkspaceId workspace;
    TagId tag;
    LibrarySnapshot original;
};

TEST_F(RepositoryRestore, RejectsDuplicateOrInvalidRecordsBeforeReplacingAnyCollection)
{
    std::vector<LibrarySnapshot> candidates;
    auto candidate = original;
    candidate.tracks.push_back(candidate.tracks.front());
    candidates.push_back(candidate);
    candidate = original;
    candidate.tracks.front().id = {};
    candidates.push_back(candidate);
    candidate = original;
    candidate.playlists.push_back(candidate.playlists.front());
    candidates.push_back(candidate);
    candidate = original;
    candidate.playlists.front().entries.front().media = {"missing"};
    candidates.push_back(candidate);
    candidate = original;
    candidate.workspaces.push_back(candidate.workspaces.front());
    candidates.push_back(candidate);
    candidate = original;
    candidate.workspaces.front().root.clear();
    candidates.push_back(candidate);
    for (const auto& invalid : candidates) {
        EXPECT_FALSE(repository.replace_all(invalid.tracks, invalid.playlists, invalid.workspaces));
        expect_original();
    }
}

TEST_F(RepositoryRestore, RejectsMalformedTrackTagsWithoutReplacingExistingMetadata)
{
    auto invalid = original.tracks.front();
    invalid.embedded.title = "Rejected";
    invalid.tags.resize(max_tags_per_track + 1);
    EXPECT_FALSE(repository.put_track(invalid));
    expect_original();
    invalid.tags = original.tracks.front().tags;
    invalid.tags.front().id = {};
    EXPECT_FALSE(repository.put_track(invalid));
    expect_original();
    invalid.tags = original.tracks.front().tags;
    invalid.tags.front().value.clear();
    EXPECT_FALSE(repository.put_track(invalid));
    expect_original();
    invalid.tags.front().value = " Calm ";
    EXPECT_FALSE(repository.put_track(invalid));
    expect_original();
}

TEST_F(RepositoryRestore, RejectsUnidentifiedPlaylistsEntriesAndWorkspaces)
{
    auto invalid_playlist = original.playlists.front();
    invalid_playlist.id = {};
    EXPECT_FALSE(repository.put_playlist(invalid_playlist));
    EXPECT_EQ(repository.playlist({}), nullptr);
    invalid_playlist = original.playlists.front();
    invalid_playlist.entries.front().id = {};
    EXPECT_FALSE(repository.put_playlist(invalid_playlist));
    auto invalid_workspace = original.workspaces.front();
    invalid_workspace.id = {};
    EXPECT_FALSE(repository.put_workspace(invalid_workspace));
    invalid_workspace = original.workspaces.front();
    invalid_workspace.root.clear();
    EXPECT_FALSE(repository.put_workspace(invalid_workspace));
    expect_original();
}

TEST_F(RepositoryRestore, MissingOccurrenceAndTagRemovalLeaveTheCatalogUntouched)
{
    EXPECT_FALSE(repository.remove_entry(PlaylistId{"missing"}, PlaylistEntryId{"missing"}));
    EXPECT_FALSE(repository.remove_tag(MediaId{"missing"}, tag));
    expect_original();
    {
        RepositoryTransaction transaction{repository, [](const auto&) { return true; }};
        EXPECT_FALSE(repository.remove_entry(playlist, PlaylistEntryId{"missing"}));
        EXPECT_FALSE(repository.remove_tag(media, TagId{"missing"}));
        EXPECT_FALSE(repository.delete_workspace(WorkspaceId{"missing"}));
        EXPECT_FALSE(repository.delete_playlist(PlaylistId{"missing"}));
        EXPECT_TRUE(transaction.commit());
    }
    expect_original();
}

TEST_F(RepositoryRestore, AbandonedMutationsRestoreReplacedAndDeletedRecords)
{
    {
        RepositoryTransaction transaction{repository, [](const auto&) { return true; }};
        auto changed = original.playlists.front();
        changed.name = "Rejected";
        ASSERT_TRUE(repository.put_playlist(changed));
        ASSERT_TRUE(repository.remove_entry(playlist, changed.entries.front().id));
        ASSERT_TRUE(repository.delete_playlist(playlist));
        ASSERT_TRUE(repository.delete_workspace(workspace));
        ASSERT_TRUE(repository.remove_tag(media, tag));
        EXPECT_EQ(repository.playlist(playlist), nullptr);
        EXPECT_EQ(repository.workspace(workspace), nullptr);
        EXPECT_TRUE(repository.track(media)->tags.empty());
    }
    expect_original();
}

TEST_F(RepositoryRestore, EntryAndWorkspaceAllocatorsSkipAllRestoredIdentities)
{
    auto restored = original;
    restored.playlists.front().entries.push_back({.id = {"entry-2"}, .media = media});
    restored.workspaces.push_back({.id = {"workspace-2"}, .root = "another"});
    ASSERT_TRUE(repository.replace_all(restored.tracks, restored.playlists, restored.workspaces));
    const auto entry = repository.append_track(playlist, media);
    ASSERT_TRUE(entry);
    EXPECT_EQ(entry->value, "entry-3");
    EXPECT_EQ(repository.create_workspace("third").value, "workspace-3");
    EXPECT_EQ(repository.playlist(playlist)->entries.size(), 3U);
    EXPECT_EQ(repository.workspaces().size(), 3U);
}

} // namespace
