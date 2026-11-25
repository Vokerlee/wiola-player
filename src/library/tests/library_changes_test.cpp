/**
 * @file
 * @brief Committed change batching, observer lifetime, and ordering tests.
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
#include <stdexcept>
#include <utility>
#include <wiola/library/repository_transaction.hpp>

namespace {

using namespace wiola::library;

TrackRecord track(const char* id = "media")
{
    TrackRecord result;
    result.id = MediaId{id};
    return result;
}

bool persisted(const LibraryRepository&)
{
    return true;
}

void rename(InMemoryRepository& repository, const PlaylistId& id, const char* name)
{
    RepositoryTransaction transaction{repository, persisted};
    ASSERT_TRUE(repository.rename_playlist(id, name));
    ASSERT_TRUE(transaction.commit());
}

TEST(LibraryChanges, PublishesOneCoherentBatchOnlyAfterPersistence)
{
    InMemoryRepository repository;
    ASSERT_TRUE(repository.put_track(track()));
    std::vector<LibraryChangeBatch> batches;
    auto subscription = repository.observe([&](const auto& batch) { batches.push_back(batch); });
    bool saved = false;
    RepositoryTransaction transaction{repository, [&](const LibraryRepository& candidate) {
                                          EXPECT_TRUE(batches.empty());
                                          EXPECT_EQ(candidate.playlists().size(), 1U);
                                          saved = true;
                                          return true;
                                      }};
    auto updated = *repository.track(MediaId{"media"});
    updated.overrides.title = "Changed";
    ++updated.metadata_revision;
    ASSERT_TRUE(repository.put_track(updated));
    const auto playlist = repository.create_playlist("Saved");
    ASSERT_TRUE(repository.append_track(playlist, updated.id));
    ASSERT_TRUE(repository.append_track(playlist, updated.id));
    const auto workspace = repository.create_workspace("music");
    ASSERT_TRUE(batches.empty());
    ASSERT_TRUE(transaction.commit());
    EXPECT_TRUE(saved);
    ASSERT_EQ(batches.size(), 1U);
    const auto& batch = batches.front();
    EXPECT_EQ(batch.revision, 1U);
    ASSERT_EQ(batch.media.size(), 1U);
    EXPECT_EQ(batch.media.front().media, updated.id);
    EXPECT_EQ(batch.media.front().metadata_revision, 1U);
    EXPECT_TRUE(batch.media.front().includes(MediaChangeFields::overrides));
    EXPECT_FALSE(batch.media.front().includes(MediaChangeFields::tags));
    EXPECT_EQ(batch.playlists, std::vector{playlist});
    EXPECT_EQ(batch.workspaces, std::vector{workspace});
    EXPECT_EQ(repository.playlist(playlist)->entries.size(), 2U);
}

TEST(LibraryChanges, FailedAndNoOpTransactionsPublishNothing)
{
    InMemoryRepository repository;
    const auto playlist = repository.create_playlist("Original");
    int events = 0;
    int saves = 0;
    auto subscription = repository.observe([&](const auto&) { ++events; });
    {
        RepositoryTransaction transaction{repository, [&](const LibraryRepository&) {
                                              ++saves;
                                              return false;
                                          }};
        ASSERT_TRUE(repository.rename_playlist(playlist, "Rejected"));
        EXPECT_FALSE(transaction.commit());
    }
    {
        RepositoryTransaction transaction{repository, [&](const LibraryRepository&) {
                                              ++saves;
                                              return true;
                                          }};
        EXPECT_TRUE(transaction.commit());
    }
    EXPECT_EQ(repository.playlist(playlist)->name, "Original");
    EXPECT_EQ(repository.change_revision(), 0U);
    EXPECT_EQ(events, 0);
    EXPECT_EQ(saves, 1);
}

TEST(LibraryChanges, RecordsRemovedIdentitiesAndNewMediaFields)
{
    InMemoryRepository repository;
    ASSERT_TRUE(repository.put_track(track()));
    const auto playlist = repository.create_playlist("Removed");
    const auto workspace = repository.create_workspace("removed");
    std::vector<LibraryChangeBatch> batches;
    auto subscription = repository.observe([&](const auto& batch) { batches.push_back(batch); });
    RepositoryTransaction transaction{repository, persisted};
    ASSERT_TRUE(repository.replace_all({track("new-media")}, {}, {}));
    ASSERT_TRUE(transaction.commit());
    ASSERT_EQ(batches.size(), 1U);
    EXPECT_EQ(batches.front().removed_media, std::vector{MediaId{"media"}});
    EXPECT_EQ(batches.front().removed_playlists, std::vector{playlist});
    EXPECT_EQ(batches.front().removed_workspaces, std::vector{workspace});
    ASSERT_EQ(batches.front().media.size(), 1U);
    EXPECT_EQ(batches.front().media.front().fields, MediaChangeFields::all);
}

TEST(LibraryChanges, ScopedObserversCanDisconnectAndMoveDuringPublication)
{
    InMemoryRepository repository;
    const auto playlist = repository.create_playlist("Original");
    LibraryChanges::Subscription second;
    int first_calls = 0;
    int second_calls = 0;
    auto first = repository.observe([&](const auto&) {
        ++first_calls;
        second.reset();
    });
    second = repository.observe([&](const auto&) { ++second_calls; });
    auto moved = std::move(first);
    rename(repository, playlist, "First");
    EXPECT_EQ(first_calls, 1);
    EXPECT_EQ(second_calls, 0);
    moved.reset();
    rename(repository, playlist, "Second");
    EXPECT_EQ(first_calls, 1);
    EXPECT_EQ(second_calls, 0);
    LibraryChanges::Subscription outlives_repository;
    {
        InMemoryRepository temporary;
        outlives_repository = temporary.observe([](const auto&) { });
    }
    outlives_repository.reset();
}

TEST(LibraryChanges, ReentrantCommitsReachEveryReceiverInRevisionOrder)
{
    InMemoryRepository repository;
    const auto playlist = repository.create_playlist("Original");
    std::vector<std::pair<int, std::uint64_t>> order;
    auto first = repository.observe([&](const auto& batch) {
        order.emplace_back(1, batch.revision);
        if (batch.revision == 1)
            rename(repository, playlist, "Nested");
    });
    auto second =
        repository.observe([&](const auto& batch) { order.emplace_back(2, batch.revision); });
    rename(repository, playlist, "Outer");
    EXPECT_EQ(order,
        (std::vector<std::pair<int, std::uint64_t>>{
            {1, 1},
            {2, 1},
            {1, 2},
            {2, 2}
    }));
    EXPECT_EQ(repository.change_revision(), 2U);
}

TEST(LibraryChanges, ObserverExceptionsDoNotRollBackDurableDataOrSkipOtherReceivers)
{
    InMemoryRepository repository;
    const auto playlist = repository.create_playlist("Original");
    int received = 0;
    auto failing = repository.observe([](const auto&) { throw std::runtime_error{"observer"}; });
    auto other = repository.observe([&](const auto&) { ++received; });
    const auto commit = [&] {
        RepositoryTransaction transaction{repository, persisted};
        repository.rename_playlist(playlist, "Committed");
        static_cast<void>(transaction.commit());
    };
    EXPECT_THROW(commit(), std::runtime_error);
    EXPECT_EQ(repository.playlist(playlist)->name, "Committed");
    EXPECT_EQ(received, 1);
    failing.reset();
    rename(repository, playlist, "Next");
    EXPECT_EQ(received, 2);
}

TEST(LibraryChanges, CandidateCopiesDoNotShareSubscriptions)
{
    InMemoryRepository original;
    const auto playlist = original.create_playlist("Original");
    int original_events = 0;
    auto subscription = original.observe([&](const auto&) { ++original_events; });
    InMemoryRepository copy = original;
    rename(copy, playlist, "Candidate");
    EXPECT_EQ(original_events, 0);
    EXPECT_EQ(original.playlist(playlist)->name, "Original");
    EXPECT_EQ(copy.playlist(playlist)->name, "Candidate");
}

TEST(LibraryChanges, RejectsNestedMutationBeforeAnOuterCommit)
{
    InMemoryRepository repository;
    RepositoryTransaction outer{repository, persisted};
    const auto nested = [&] {
        RepositoryTransaction inner{repository, persisted};
    };
    EXPECT_THROW(nested(), std::logic_error);
    const auto id = repository.create_playlist("Retained");
    ASSERT_TRUE(outer.commit());
    EXPECT_NE(repository.playlist(id), nullptr);
}

} // namespace
