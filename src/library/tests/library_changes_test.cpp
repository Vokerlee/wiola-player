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

#include <wiola/library/repository_transaction.hpp>

#include <gtest/gtest.h>

#include <stdexcept>
#include <utility>

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

TEST(LibraryChanges, RegisteredMutableCallbackStatePersistsAcrossRevisions)
{
    InMemoryRepository repository;
    const auto playlist = repository.create_playlist("Original");
    std::vector<int> counts;
    auto subscription = repository.observe([count = 0, &counts](const auto&) mutable {
        counts.push_back(++count);
    });

    rename(repository, playlist, "First");
    rename(repository, playlist, "Second");
    EXPECT_EQ(counts, (std::vector{1, 2}));
}

struct CopySensitiveObserver {
    bool& reject_copy;
    int& calls;

    CopySensitiveObserver(bool& reject, int& count)
        : reject_copy{reject}
        , calls{count}
    {
    }

    CopySensitiveObserver(const CopySensitiveObserver& other)
        : reject_copy{other.reject_copy}
        , calls{other.calls}
    {
        if (reject_copy)
            throw std::runtime_error{"observer copied during publication"};
    }

    CopySensitiveObserver(CopySensitiveObserver&&) = default;

    void operator()(const LibraryChangeBatch&) const { ++calls; }
};

TEST(LibraryChanges, PublicationDoesNotCopyRegisteredCallbackState)
{
    InMemoryRepository repository;
    const auto playlist = repository.create_playlist("Original");
    bool reject_copy = false;
    int sensitive_calls = 0, healthy_calls = 0;
    auto sensitive = repository.observe(CopySensitiveObserver{reject_copy, sensitive_calls});
    auto healthy = repository.observe([&](const auto&) { ++healthy_calls; });

    reject_copy = true;
    EXPECT_NO_THROW(rename(repository, playlist, "Committed"));
    EXPECT_EQ(sensitive_calls, 1);
    EXPECT_EQ(healthy_calls, 1);
    EXPECT_EQ(repository.playlist(playlist)->name, "Committed");
}

TEST(LibraryChanges, NewSubscriptionsStartWithTheNextBatchIncludingNestedCommits)
{
    InMemoryRepository repository;
    const auto playlist = repository.create_playlist("Original");
    std::vector<std::uint64_t> received;
    LibraryChanges::Subscription added;
    auto first = repository.observe([&](const auto& batch) {
        if (batch.revision == 1) {
            added =
                repository.observe([&](const auto& next) { received.push_back(next.revision); });
            rename(repository, playlist, "Nested");
        }
    });

    rename(repository, playlist, "Outer");
    EXPECT_EQ(received, std::vector<std::uint64_t>{2});
}

TEST(LibraryChanges, SelfDisconnectRetainsTheActiveCallbackUntilItReturns)
{
    InMemoryRepository repository;
    const auto playlist = repository.create_playlist("Original");
    auto retained = std::make_shared<int>(7);
    const std::weak_ptr<int> weak = retained;
    LibraryChanges::Subscription subscription;
    subscription = repository.observe([retained, &subscription, &weak](const auto&) {
        subscription.reset();
        EXPECT_FALSE(weak.expired());
        EXPECT_EQ(*retained, 7);
    });
    retained.reset();
    rename(repository, playlist, "Committed");
    EXPECT_TRUE(weak.expired());
}

TEST(LibraryChanges, SubscriptionSelfMovePreservesItsConnection)
{
    InMemoryRepository repository;
    const auto playlist = repository.create_playlist("Original");
    int calls = 0;
    auto subscription = repository.observe([&](const auto&) { ++calls; });
    auto& alias = subscription;
    subscription = std::move(alias);
    rename(repository, playlist, "Committed");
    EXPECT_EQ(calls, 1);
}

TEST(LibraryChanges, RejectsEmptyObserversAndRethrowsTheFirstOfSeveralFailures)
{
    InMemoryRepository repository;

    EXPECT_THROW(static_cast<void>(repository.observe({})), std::invalid_argument);

    const auto playlist = repository.create_playlist("Original");
    int calls = 0;
    auto first = repository.observe([](const auto&) { throw std::runtime_error{"first"}; });
    auto second = repository.observe([](const auto&) { throw std::logic_error{"second"}; });
    auto healthy = repository.observe([&](const auto&) { ++calls; });

    try {
        rename(repository, playlist, "Committed");
        FAIL() << "observer failure must reach the publisher";
    } catch (const std::runtime_error& error) {
        EXPECT_STREQ(error.what(), "first");
    }

    EXPECT_EQ(calls, 1);
}

TEST(LibraryChanges, EveryChangedOrRemovedCollectionMakesTheBatchNonempty)
{
    EXPECT_TRUE(LibraryChangeBatch{}.empty());

    const std::vector<LibraryChangeBatch> batches{
        {.media = {MediaChange{}}},
        {.removed_media = {MediaId{"media"}}},
        {.playlists = {PlaylistId{"playlist"}}},
        {.removed_playlists = {PlaylistId{"removed"}}},
        {.workspaces = {WorkspaceId{"workspace"}}},
        {.removed_workspaces = {WorkspaceId{"removed"}}},
    };

    for (const auto& batch : batches)
        EXPECT_FALSE(batch.empty());
}

} // namespace
