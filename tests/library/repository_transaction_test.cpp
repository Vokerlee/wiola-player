/**
 * @file
 * @brief Persistent track, metadata, tag, and playlist record shapes.
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
#include <library/repository_transaction.hpp>
#include <stdexcept>

namespace {

using namespace wiola::library;

TrackRecord track()
{
    TrackRecord value;
    value.id = MediaId{"media"};
    value.embedded.title = "Original";
    value.metadata_revision = 7;
    return value;
}

TEST(RepositoryTransaction, PersistenceFailureRestoresValuesRevisionsAndEveryAllocator)
{
    InMemoryRepository repository;
    ASSERT_TRUE(repository.put_track(track()));
    PlaylistId playlist;
    PlaylistEntryId entry;
    WorkspaceId workspace;
    TagId tag;
    RepositoryTransaction transaction{repository, [](const LibraryRepository& candidate) {
                                          EXPECT_EQ(candidate.playlists().size(), 1U);
                                          return false;
                                      }};
    playlist = repository.create_playlist("Temporary");
    entry = *repository.append_track(playlist, MediaId{"media"});
    workspace = repository.create_workspace("temporary");
    tag = repository.add_tag(MediaId{"media"}, "Mood", "Calm");
    EXPECT_FALSE(transaction.commit());
    EXPECT_TRUE(repository.playlists().empty());
    EXPECT_TRUE(repository.workspaces().empty());
    ASSERT_NE(repository.track(MediaId{"media"}), nullptr);
    EXPECT_TRUE(repository.track(MediaId{"media"})->tags.empty());
    EXPECT_EQ(repository.track(MediaId{"media"})->metadata_revision, 7U);
    EXPECT_EQ(repository.create_playlist("Retained"), playlist);
    EXPECT_EQ(repository.append_track(playlist, MediaId{"media"}), entry);
    EXPECT_EQ(repository.create_workspace("retained"), workspace);
    EXPECT_EQ(repository.add_tag(MediaId{"media"}, "Mood", "Calm"), tag);
}

TEST(RepositoryTransaction, AbandoningACandidateDoesNotCallPersistence)
{
    InMemoryRepository repository;
    int writes = 0;
    {
        RepositoryTransaction transaction{repository, [&writes](const LibraryRepository&) {
                                              ++writes;
                                              return true;
                                          }};
        repository.create_playlist("Discarded");
    }
    EXPECT_EQ(writes, 0);
    EXPECT_TRUE(repository.playlists().empty());
    EXPECT_EQ(repository.create_playlist("Next").value, "playlist-1");
}

TEST(RepositoryTransaction, ExceptionDuringPersistenceRollsBack)
{
    InMemoryRepository repository;
    const auto fail = [&repository] {
        RepositoryTransaction transaction{repository,
            [](const LibraryRepository&) -> bool { throw std::runtime_error{"disk failure"}; }};
        repository.create_playlist("Discarded");
        static_cast<void>(transaction.commit());
    };
    EXPECT_THROW(fail(), std::runtime_error);
    EXPECT_TRUE(repository.playlists().empty());
    EXPECT_EQ(repository.create_playlist("Next").value, "playlist-1");
}

TEST(RepositoryTransaction, SuccessfulCommitCannotBeUndoneOrCommittedTwice)
{
    InMemoryRepository repository;
    RepositoryTransaction transaction{repository, [](const LibraryRepository&) { return true; }};
    const auto id = repository.create_playlist("Retained");
    ASSERT_TRUE(transaction.commit());
    transaction.rollback();
    EXPECT_NE(repository.playlist(id), nullptr);
    EXPECT_THROW(static_cast<void>(transaction.commit()), std::logic_error);
}

TEST(RepositoryTransaction, RejectsAnImplicitPersistencePolicy)
{
    InMemoryRepository repository;
    EXPECT_THROW((RepositoryTransaction{repository, {}}), std::invalid_argument);
    EXPECT_TRUE(repository.playlists().empty());
}

} // namespace
