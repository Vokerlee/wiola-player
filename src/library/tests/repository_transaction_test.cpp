/**
 * @file
 * @brief Repository rollback, allocation, and persistence contract tests.
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

TEST(RepositoryTransaction, OnlyTouchedRecordsMoveAndTheImmutableDeltaSurvivesPublication)
{
    InMemoryRepository repository;
    for (int i = 0; i < 1000; ++i) {
        auto value = track();
        value.id = {"media-" + std::to_string(i)};
        ASSERT_TRUE(repository.put_track(value));
    }
    const auto* untouched = repository.track(MediaId{"media-999"});
    std::shared_ptr<const RevisionDelta> accepted;
    RepositoryTransaction transaction{repository, [&](const LibraryRepository&) {
                                          accepted = repository.staged_delta();
                                          return true;
                                      }};
    auto changed = *repository.track(MediaId{"media-0"});
    changed.embedded.title = "Updated";
    ASSERT_TRUE(repository.put_track(changed));
    ASSERT_TRUE(transaction.commit());
    ASSERT_TRUE(accepted);
    ASSERT_EQ(accepted->tracks.size(), 1);
    EXPECT_EQ(accepted->changes.media.size(), 1);
    EXPECT_EQ(accepted->tracks[0].id, changed.id);
    EXPECT_EQ(repository.track(MediaId{"media-999"}), untouched);
    EXPECT_EQ(accepted->changes.revision, 1);
}

TEST(RepositoryTransaction,
    BulkReplacementAndRepeatedEditsRestoreEveryOriginalRecordWithoutAllocatingOnRollback)
{
    InMemoryRepository repository;
    ASSERT_TRUE(repository.put_track(track()));
    const auto playlist = repository.create_playlist("Original");
    const auto entry = repository.append_track(playlist, MediaId{"media"});
    ASSERT_TRUE(entry);
    const auto* original = repository.track(MediaId{"media"});
    {
        RepositoryTransaction transaction{repository, [](const auto&) { return false; }};
        auto next = track();
        next.id = {"other"};
        ASSERT_TRUE(repository.replace_all({next}, {}, {}));
        ASSERT_TRUE(repository.put_track(next));
        repository.create_playlist("Discarded");
        EXPECT_FALSE(transaction.commit());
    }
    EXPECT_EQ(repository.track(MediaId{"media"}), original);
    EXPECT_EQ(repository.track(MediaId{"other"}), nullptr);
    ASSERT_NE(repository.playlist(playlist), nullptr);
    EXPECT_EQ(repository.playlist(playlist)->entries[0].id, *entry);
    EXPECT_EQ(repository.playlist(playlist)->name, "Original");
}

TEST(RepositoryTransaction,
    AcceptedReceiptReportsFailedAttemptsThenDurableWithoutRollingBackLaterEdits)
{
    InMemoryRepository repository;
    auto receipt = std::make_shared<CommitReceipt>(1);
    {
        RepositoryTransaction transaction{repository,
            [&](const auto&) { return CommitResult::accepted(1, receipt); }};
        ASSERT_TRUE(repository.put_track(track()));
        const auto result = transaction.commit();
        EXPECT_EQ(result.state, CommitState::accepted);
        EXPECT_FALSE(result.durable());
    }
    auto changed = track();
    changed.embedded.title = "Later edit";
    {
        RepositoryTransaction transaction{repository, [](const auto&) { return true; }};
        ASSERT_TRUE(repository.put_track(changed));
        ASSERT_TRUE(transaction.commit());
    }
    std::vector<bool> notifications;
    receipt->observe([&](auto event) {
        EXPECT_EQ(event.revision, 1);
        notifications.push_back(event.durable);
    });
    receipt->notify(false);
    EXPECT_EQ(repository.track(MediaId{"media"})->embedded.title, "Later edit");
    receipt->notify(true);
    EXPECT_EQ(notifications, (std::vector<bool>{false, true}));
}

} // namespace
