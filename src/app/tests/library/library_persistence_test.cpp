/**
 * @file
 * @brief Portable persistence acceptance, retry, delivery and lifecycle contracts.
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

#include <algorithm>
#include <allocation_failure.hpp>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <gtest/gtest.h>
#include <limits>
#include <mutex>
#include <new>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <vector>
#include <wiola/app/library/library_persistence.hpp>

namespace {

using namespace wiola;
static_assert(!std::is_copy_constructible_v<app::LibraryPersistence>);
static_assert(!std::is_move_constructible_v<app::LibraryPersistence>);

class ManualDelivery final {
public:
    app::LibraryPersistence::Delivery callbacks()
    {
        return {
            [this](std::function<void()> task) {
                std::lock_guard lock{mutex_};
                pending_.push_back(std::move(task));
                changed_.notify_all();
                return true;
            },
            [this] { drain(); },
        };
    }

    bool wait_pending(std::size_t count)
    {
        std::unique_lock lock{mutex_};
        return changed_.wait_for(lock, std::chrono::seconds{5},
            [&] { return pending_.size() >= count; });
    }

    void reverse_pending()
    {
        std::lock_guard lock{mutex_};
        std::reverse(pending_.begin(), pending_.end());
    }

    void drain()
    {
        for (;;) {
            std::function<void()> task;
            {
                std::lock_guard lock{mutex_};
                if (pending_.empty())
                    return;
                task = std::move(pending_.front());
                pending_.pop_front();
            }
            task();
        }
    }

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<std::function<void()>> pending_;
};

class ControlledWriter final : public app::SnapshotWriter {
public:
    app::SnapshotWriteResult write(const library::LibrarySnapshot& snapshot) override
    {
        std::unique_lock lock{mutex_};
        snapshots_.push_back(snapshot);
        changed_.notify_all();
        changed_.wait(lock, [this] { return !blocked_; });
        if (throws_)
            throw std::runtime_error{"controlled storage failure"};
        return result_;
    }

    void block()
    {
        std::lock_guard lock{mutex_};
        blocked_ = true;
    }

    void release(app::SnapshotWriteResult result = {}, bool throws = false)
    {
        std::lock_guard lock{mutex_};
        result_ = result;
        throws_ = throws;
        blocked_ = false;
        changed_.notify_all();
    }

    bool wait_entered()
    {
        std::unique_lock lock{mutex_};
        return changed_.wait_for(lock, std::chrono::seconds{5},
            [this] { return !snapshots_.empty(); });
    }

    library::LibrarySnapshot last_snapshot() const
    {
        std::lock_guard lock{mutex_};
        return snapshots_.back();
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::vector<library::LibrarySnapshot> snapshots_;
    app::SnapshotWriteResult result_;
    bool blocked_{}, throws_{};
};

std::shared_ptr<library::RevisionDelta> revision(std::uint64_t number, const char* id = "media")
{
    auto delta = std::make_shared<library::RevisionDelta>();
    delta->changes.revision = number;
    library::TrackRecord track;
    track.id = {id};
    track.embedded.title = id;
    delta->tracks.push_back(std::move(track));
    return delta;
}

class LibraryPersistenceCore : public ::testing::Test {
protected:
    ~LibraryPersistenceCore() override
    {
        store.release(); // Unblock before joining even when a test assertion exits early.
        writer.reset();
    }

    void start(library::LibrarySnapshot initial = {},
        app::LibraryPersistence::Observer observer = {}, app::LibraryPersistenceLimits limits = {})
    {
        writer.reset();
        writer = std::make_unique<app::LibraryPersistence>(store, delivery.callbacks(),
            std::move(initial), std::move(observer), limits);
    }

    ManualDelivery delivery;
    ControlledWriter store;
    std::unique_ptr<app::LibraryPersistence> writer;
};

TEST_F(LibraryPersistenceCore, RejectsNullSkippedAndRepeatedRevisionsWithoutLosingTheNextEdit)
{
    start();
    EXPECT_FALSE(writer->accept({}));
    EXPECT_FALSE(writer->accept(revision(2)));
    EXPECT_EQ(writer->pending_revision(), 0U);
    const auto accepted = writer->accept(revision(1));
    ASSERT_TRUE(accepted);
    EXPECT_EQ(accepted.state, library::CommitState::accepted);
    EXPECT_FALSE(writer->accept(revision(1)));
    ASSERT_TRUE(writer->flush().succeeded());
    EXPECT_TRUE(accepted.receipt->durable());
    EXPECT_EQ(writer->durable_revision(), 1U);
    writer->stop();
    writer->stop();
    EXPECT_TRUE(writer->flush().succeeded());
    EXPECT_FALSE(writer->accept(revision(2)));
}

TEST_F(LibraryPersistenceCore, InitialSnapshotAndRemovalFactsReachTheSuppliedWriter)
{
    library::LibrarySnapshot initial;
    auto first = revision(1, "retained")->tracks.front();
    auto removed = revision(1, "removed")->tracks.front();
    initial.tracks = {first, removed};
    initial.playlists = {
        {.id = {"playlist"}, .name = "Saved"}
    };
    initial.workspaces = {
        {.id = {"workspace"}, .root = "observed"}
    };
    start(initial);
    auto delta = revision(1, "added");
    delta->changes.removed_media = {removed.id};
    delta->changes.removed_playlists = {initial.playlists.front().id};
    delta->changes.removed_workspaces = {initial.workspaces.front().id};
    const auto accepted = writer->accept(delta);
    ASSERT_TRUE(accepted);
    ASSERT_TRUE(writer->flush().succeeded());
    const auto saved = store.last_snapshot();
    ASSERT_EQ(saved.tracks.size(), 2U);
    EXPECT_EQ(saved.tracks.front().id.value, "added");
    EXPECT_EQ(saved.tracks.back(), first);
    EXPECT_TRUE(saved.playlists.empty());
    EXPECT_TRUE(saved.workspaces.empty());
}

TEST_F(LibraryPersistenceCore, FailedAttemptsRetainBothAcceptedEditsForSuccessfulRetry)
{
    store.block();
    start();
    const auto first = writer->accept(revision(1, "first"));
    ASSERT_TRUE(first);
    ASSERT_TRUE(store.wait_entered());
    const auto second = writer->accept(revision(2, "second"));
    ASSERT_TRUE(second);
    std::vector<bool> first_events, second_events;
    first.receipt->observe([&](auto event) { first_events.push_back(event.durable); });
    second.receipt->observe([&](auto event) { second_events.push_back(event.durable); });
    bool attempt_saved = true;
    writer->when_durable(2, [&](bool saved) { attempt_saved = saved; });
    store.release({app::SnapshotWriteError::io});
    EXPECT_FALSE(writer->flush().succeeded());
    EXPECT_FALSE(attempt_saved);
    EXPECT_EQ(writer->pending_revision(), 2U);
    EXPECT_EQ(writer->durable_revision(), 0U);
    store.release();
    writer->retry();
    ASSERT_TRUE(writer->flush().succeeded());
    ASSERT_FALSE(first_events.empty());
    ASSERT_FALSE(second_events.empty());
    EXPECT_FALSE(first_events.front());
    EXPECT_FALSE(second_events.front());
    EXPECT_TRUE(first_events.back());
    EXPECT_TRUE(second_events.back());
    EXPECT_EQ(store.last_snapshot().tracks.size(), 2U);
    EXPECT_EQ(writer->durable_revision(), 2U);
}

TEST_F(LibraryPersistenceCore, RuntimeFailureAtTheDriverBoundaryLeavesTheRevisionRetryable)
{
    store.release({}, true);
    start();
    const auto accepted = writer->accept(revision(1));
    ASSERT_TRUE(accepted);
    EXPECT_EQ(writer->flush().error, app::SnapshotWriteError::io);
    EXPECT_FALSE(accepted.receipt->durable());
    store.release();
    ASSERT_TRUE(writer->flush().succeeded());
    EXPECT_TRUE(accepted.receipt->durable());
}

TEST_F(LibraryPersistenceCore, DurabilityWaitersDistinguishPendingSavedAndUnavailableRevisions)
{
    store.block();
    start();
    ASSERT_TRUE(writer->accept(revision(1)));
    ASSERT_TRUE(store.wait_entered());
    std::vector<bool> results;
    writer->when_durable(1, [&](bool saved) { results.push_back(saved); });
    EXPECT_TRUE(results.empty());
    writer->when_durable(2, [&](bool saved) { results.push_back(saved); });
    EXPECT_EQ(results, std::vector<bool>{false});
    store.release();
    ASSERT_TRUE(writer->flush().succeeded());
    writer->when_durable(1, [&](bool saved) { results.push_back(saved); });
    EXPECT_EQ(results, (std::vector<bool>{false, true, true}));
    writer->stop();
    writer->when_durable(2, [&](bool saved) { results.push_back(saved); });
    EXPECT_FALSE(results.back());
}

TEST_F(LibraryPersistenceCore, DestructionCancelsReceiptsAndDisarmsQueuedCallbacks)
{
    store.block();
    start();
    const auto accepted = writer->accept(revision(1));
    ASSERT_TRUE(accepted);
    ASSERT_TRUE(store.wait_entered());
    int calls = 0;
    accepted.receipt->observe([&](auto) { ++calls; });
    writer->when_durable(1, [&](bool) { ++calls; });
    store.release();
    writer.reset();
    delivery.drain();
    accepted.receipt->observe([&](auto) { ++calls; });
    EXPECT_EQ(calls, 0);
}

TEST_F(LibraryPersistenceCore, RejectsCrossThreadOwnerOperationsAndKeepsTheWorkflowUsable)
{
    start();
    int rejected = 0;
    std::thread other{[&] {
        for (int operation = 0; operation < 4; ++operation) {
            try {
                switch (operation) {
                case 0:
                    static_cast<void>(writer->accept(revision(1)));
                    break;
                case 1:
                    writer->when_durable(0, [](bool) { });
                    break;
                case 2:
                    static_cast<void>(writer->flush());
                    break;
                case 3:
                    writer->stop();
                    break;
                }
            } catch (const std::logic_error&) {
                ++rejected;
            }
        }
    }};
    other.join();
    EXPECT_EQ(rejected, 4);
    ASSERT_TRUE(writer->accept(revision(1)));
    EXPECT_TRUE(writer->flush().succeeded());
}

TEST_F(LibraryPersistenceCore, RequiresBothOwnerDeliveryOperationsBeforeStartingTheWorker)
{
    EXPECT_THROW((app::LibraryPersistence{store, {}, {}}), std::invalid_argument);
    auto callbacks = delivery.callbacks();
    callbacks.drain = {};
    EXPECT_THROW((app::LibraryPersistence{store, callbacks, {}}), std::invalid_argument);
}

TEST_F(LibraryPersistenceCore, RetainedCapacityCannotBypassThePayloadBudget)
{
    app::LibraryPersistenceLimits limits;
    limits.pending_payload_bytes =
        sizeof(library::RevisionDelta) + 8 * sizeof(library::TrackRecord);
    for (const bool nested_text : {false, true}) {
        start({}, {}, limits);
        auto delta = revision(1);
        if (nested_text)
            delta->tracks.front().embedded.comment.reserve(limits.pending_payload_bytes + 1);
        else
            delta->tracks.reserve(limits.pending_payload_bytes / sizeof(library::TrackRecord) + 1);
        bool accepted = false;
        bool allocation_failed = false;
        {
#if WIOLA_TEST_ALLOCATOR_REPLACEMENT
            const wiola::testing::AllocationFailure failure{0};
#endif
            try {
                accepted = static_cast<bool>(writer->accept(delta));
            } catch (const std::bad_alloc&) {
                allocation_failed = true;
            }
        }
        EXPECT_FALSE(accepted);
        EXPECT_FALSE(allocation_failed);
        EXPECT_EQ(writer->pending_revision(), 0U);
        const auto recovery = writer->accept(revision(1));
        ASSERT_TRUE(recovery);
        ASSERT_TRUE(writer->flush().succeeded());
        EXPECT_TRUE(recovery.receipt->durable());
    }
}

TEST_F(LibraryPersistenceCore, ExactHeaderCapacityAdmitsAnEmptyRevision)
{
    auto delta = std::make_shared<library::RevisionDelta>();
    delta->changes.revision = 1;
    app::LibraryPersistenceLimits limits;
    limits.pending_payload_bytes = sizeof(*delta);
    limits.file_result_extra_bytes = 0;
    start({}, {}, limits);

    const auto accepted = writer->accept(delta);

    ASSERT_TRUE(accepted);
    EXPECT_TRUE(writer->flush().succeeded());
    EXPECT_TRUE(accepted.receipt->durable());
}

TEST_F(LibraryPersistenceCore, PhysicalResultMayUseTheLastByteOfItsReservedCapacity)
{
    auto delta = std::make_shared<library::RevisionDelta>();
    delta->changes.revision = 1;
    app::LibraryPersistenceLimits limits;
    limits.pending_payload_bytes = sizeof(*delta) - 1;
    limits.file_result_extra_bytes = 1;
    start({}, {}, limits);
    EXPECT_FALSE(writer->accept(delta));
    EXPECT_EQ(writer->pending_revision(), 0U);

    delta->priority = library::CommitPriority::file_result;
    const auto accepted = writer->accept(delta);

    ASSERT_TRUE(accepted);
    EXPECT_TRUE(writer->flush().succeeded());
    EXPECT_TRUE(accepted.receipt->durable());
}

TEST_F(LibraryPersistenceCore, InvalidCapacitiesAreRejectedBeforeStartingTheWorker)
{
    const auto rejected = [&](app::LibraryPersistenceLimits limits) {
        EXPECT_THROW((app::LibraryPersistence{store, delivery.callbacks(), {}, {}, limits}),
            std::invalid_argument);
    };
    app::LibraryPersistenceLimits limits;
    limits.pending_revisions = 0;
    rejected(limits);
    limits = {};
    limits.outstanding_receipts = 0;
    rejected(limits);
    limits = {};
    limits.pending_payload_bytes = 0;
    rejected(limits);
    limits = {};
    limits.pending_payload_bytes = std::numeric_limits<std::size_t>::max();
    limits.file_result_extra_bytes = 1;
    rejected(limits);
}

TEST_F(LibraryPersistenceCore, ReservesReceiptCapacityUntilOwnerAcknowledgment)
{
    app::LibraryPersistenceLimits limits;
    limits.outstanding_receipts = 2;
    store.block();
    start({}, {}, limits);
    ASSERT_TRUE(writer->accept(revision(1, "first")));
    ASSERT_TRUE(store.wait_entered());
    EXPECT_FALSE(writer->accept(revision(2, "ordinary")));
    auto physical = revision(2, "physical");
    physical->priority = library::CommitPriority::file_result;
    ASSERT_TRUE(writer->accept(physical));
    auto overflow = revision(3, "overflow");
    overflow->priority = library::CommitPriority::file_result;
    EXPECT_FALSE(writer->accept(overflow));
    store.release();
    ASSERT_TRUE(writer->flush().succeeded());
    ASSERT_TRUE(writer->accept(revision(3, "ordinary")));
    EXPECT_TRUE(writer->flush().succeeded());
}

TEST_F(LibraryPersistenceCore, ReservesExtraPayloadCapacityForPhysicalFileResults)
{
    app::LibraryPersistenceLimits limits;
    limits.pending_payload_bytes =
        sizeof(library::RevisionDelta) + 8 * sizeof(library::TrackRecord);
    limits.file_result_extra_bytes = limits.pending_payload_bytes;
    start({}, {}, limits);
    auto delta = revision(1);
    delta->tracks.front().embedded.comment.reserve(limits.pending_payload_bytes + 1);
    EXPECT_FALSE(writer->accept(delta));
    delta->priority = library::CommitPriority::file_result;
    ASSERT_TRUE(writer->accept(delta));
    EXPECT_TRUE(writer->flush().succeeded());
}

TEST_F(LibraryPersistenceCore, OwnedDeltaUpdatesEverySnapshotCollection)
{
    std::vector<std::uint64_t> acknowledgments;
    start({}, [&](auto number, auto result) {
        EXPECT_TRUE(result.succeeded());
        acknowledgments.push_back(number);
    });
    auto delta = revision(1);
    auto& track = delta->tracks.front();
    track.fingerprint = "observed-fingerprint";
    track.locations = {"observed/track.flac"};
    track.tags = {
        {.id = {"tag"}, .key = "Mood", .value = "Calm"}
    };
    track.overrides.title = "Application title";
    delta->playlists = {
        {.id = {"saved"}, .name = "Saved", .entries = {{.id = {"occurrence"}, .media = track.id}}}
    };
    delta->workspaces = {
        {.id = {"workspace"}, .root = "observed"}
    };
    delta->changes.media = {{.media = track.id}};
    delta->changes.playlists = {delta->playlists.front().id};
    delta->changes.workspaces = {delta->workspaces.front().id};
    ASSERT_TRUE(writer->accept(delta));
    ASSERT_TRUE(writer->flush().succeeded());
    const auto saved = store.last_snapshot();
    EXPECT_EQ(saved.tracks, delta->tracks);
    EXPECT_EQ(saved.playlists, delta->playlists);
    EXPECT_EQ(saved.workspaces, delta->workspaces);
    EXPECT_EQ(acknowledgments, std::vector<std::uint64_t>{1});
}

TEST_F(LibraryPersistenceCore, FlushSavesTheInitialSnapshotWithoutInventingAnEditRevision)
{
    library::LibrarySnapshot initial;
    initial.tracks = revision(1)->tracks;
    int acknowledgments = 0;
    start(initial, [&](auto number, auto result) {
        EXPECT_EQ(number, 0U);
        EXPECT_TRUE(result.succeeded());
        ++acknowledgments;
    });
    ASSERT_TRUE(writer->flush().succeeded());
    EXPECT_EQ(writer->pending_revision(), 0U);
    EXPECT_EQ(writer->durable_revision(), 0U);
    EXPECT_EQ(store.last_snapshot().tracks, initial.tracks);
    writer->retry();
    EXPECT_TRUE(writer->flush().succeeded());
    EXPECT_EQ(acknowledgments, 1);
}

TEST_F(LibraryPersistenceCore, ReservesTheFinalPendingSlotForPhysicalFileResults)
{
    app::LibraryPersistenceLimits limits;
    limits.pending_revisions = 3;
    store.block();
    start({}, {}, limits);
    ASSERT_TRUE(writer->accept(revision(1)));
    ASSERT_TRUE(store.wait_entered());
    std::uint64_t next = 2;
    for (std::size_t queued = 0; queued < limits.pending_revisions - 1; ++queued)
        ASSERT_TRUE(writer->accept(revision(next++)));
    EXPECT_FALSE(writer->accept(revision(next)));
    auto physical = revision(next++);
    physical->priority = library::CommitPriority::file_result;
    ASSERT_TRUE(writer->accept(physical));
    auto overflow = revision(next);
    overflow->priority = library::CommitPriority::file_result;
    EXPECT_FALSE(writer->accept(overflow));
    store.release();
    EXPECT_TRUE(writer->flush().succeeded());
    EXPECT_EQ(writer->durable_revision(), next - 1);
}

TEST_F(LibraryPersistenceCore, QueuedPayloadBudgetIsReleasedAfterThePendingPrefixIsWritten)
{
    app::LibraryPersistenceLimits limits;
    limits.pending_payload_bytes =
        sizeof(library::RevisionDelta) + 4 * sizeof(library::TrackRecord);
    store.block();
    start({}, {}, limits);
    ASSERT_TRUE(writer->accept(revision(1)));
    ASSERT_TRUE(store.wait_entered());
    std::uint64_t next = 2;
    while (writer->accept(revision(next)))
        ++next;
    EXPECT_GT(next, 2U);
    EXPECT_LT(next, limits.pending_revisions);
    EXPECT_EQ(writer->pending_revision(), next - 1);
    store.release();
    ASSERT_TRUE(writer->flush().succeeded());
    ASSERT_TRUE(writer->accept(revision(next)));
    EXPECT_TRUE(writer->flush().succeeded());
}

TEST_F(LibraryPersistenceCore, ReorderedOldFailureCannotRegressASuccessfullyAcknowledgedPrefix)
{
    store.block();
    start();
    const auto first = writer->accept(revision(1, "first"));
    ASSERT_TRUE(first);
    ASSERT_TRUE(store.wait_entered());
    std::vector<bool> first_events;
    first.receipt->observe([&](auto event) { first_events.push_back(event.durable); });
    store.release({app::SnapshotWriteError::io});
    ASSERT_TRUE(delivery.wait_pending(1));
    store.release();
    const auto second = writer->accept(revision(2, "second"));
    ASSERT_TRUE(second);
    ASSERT_TRUE(delivery.wait_pending(2));
    delivery.reverse_pending();
    ASSERT_TRUE(writer->flush().succeeded());
    EXPECT_EQ(first_events, std::vector<bool>{true});
    EXPECT_TRUE(first.receipt->durable());
    EXPECT_TRUE(second.receipt->durable());
    EXPECT_EQ(writer->durable_revision(), 2U);
    EXPECT_EQ(store.last_snapshot().tracks.size(), 2U);
}

TEST_F(LibraryPersistenceCore, ReceiptCallbackCanDisposeTheWorkflowAndCancelRemainingDelivery)
{
    start();
    const auto accepted = writer->accept(revision(1));
    ASSERT_TRUE(accepted);
    int remaining = 0;
    accepted.receipt->observe([&](auto) { writer.reset(); });
    accepted.receipt->observe([&](auto) { ++remaining; });
    const auto result = writer->flush();
    EXPECT_TRUE(result.succeeded());
    EXPECT_FALSE(writer);
    EXPECT_EQ(remaining, 0);
}

TEST_F(LibraryPersistenceCore, DurabilityWaiterCanDisposeTheWorkflowAndCancelRemainingDelivery)
{
    store.block();
    start();
    ASSERT_TRUE(writer->accept(revision(1)));
    ASSERT_TRUE(store.wait_entered());
    int remaining = 0;
    writer->when_durable(1, [&](bool) { writer.reset(); });
    writer->when_durable(1, [&](bool) { ++remaining; });
    store.release();
    const auto result = writer->flush();
    EXPECT_TRUE(result.succeeded());
    EXPECT_FALSE(writer);
    EXPECT_EQ(remaining, 0);
}

TEST_F(LibraryPersistenceCore, SaveObserverCanDisposeTheWorkflowDuringFlush)
{
    start({}, [&](auto, auto) { writer.reset(); });
    ASSERT_TRUE(writer->accept(revision(1)));
    const auto result = writer->flush();
    EXPECT_TRUE(result.succeeded());
    EXPECT_FALSE(writer);
}

TEST_F(LibraryPersistenceCore, MutableSaveObserverStatePersistsAcrossAttempts)
{
    std::vector<int> calls;
    start({}, [count = 0, &calls](auto, auto) mutable { calls.push_back(++count); });
    ASSERT_TRUE(writer->accept(revision(1)));
    ASSERT_TRUE(writer->flush().succeeded());
    ASSERT_TRUE(writer->accept(revision(2)));
    ASSERT_TRUE(writer->flush().succeeded());
    EXPECT_EQ(calls, (std::vector{1, 2}));
}

TEST_F(LibraryPersistenceCore, ObserverFailureDoesNotStrandTheOtherAcknowledgmentsInTheBatch)
{
    store.block();
    int saved = 0, receipt_calls = 0, waiter_calls = 0;
    start({}, [&](auto, auto result) {
        EXPECT_TRUE(result.succeeded());
        ++saved;
    });
    const auto first = writer->accept(revision(1, "first"));
    ASSERT_TRUE(first);
    ASSERT_TRUE(store.wait_entered());
    const auto second = writer->accept(revision(2, "second"));
    ASSERT_TRUE(second);
    first.receipt->observe([](auto) { throw std::runtime_error{"first receipt"}; });
    second.receipt->observe([&](auto) { ++receipt_calls; });
    writer->when_durable(2, [](bool) { throw std::logic_error{"first waiter"}; });
    writer->when_durable(2, [&](bool) { ++waiter_calls; });
    store.release();
    ASSERT_TRUE(delivery.wait_pending(2));
    delivery.reverse_pending(); // Exercise the coalesced prefix before its older wakeup.
    EXPECT_THROW(static_cast<void>(writer->flush()), std::runtime_error);
    EXPECT_TRUE(first.receipt->durable());
    EXPECT_TRUE(second.receipt->durable());
    EXPECT_EQ(receipt_calls, 1);
    EXPECT_EQ(waiter_calls, 1);
    EXPECT_EQ(saved, 1);
}

TEST_F(LibraryPersistenceCore, IgnoresEmptyDurabilityCallbacks)
{
    store.block();
    start();
    ASSERT_TRUE(writer->accept(revision(1)));
    ASSERT_TRUE(store.wait_entered());
    writer->when_durable(1, {});
    store.release();
    EXPECT_NO_THROW(static_cast<void>(writer->flush()));
    EXPECT_NO_THROW(writer->when_durable(1, {}));
    EXPECT_NO_THROW(writer->when_durable(2, {}));
}

TEST_F(LibraryPersistenceCore, AcknowledgmentReleasesReceiptCapacityForAReentrantEdit)
{
    app::LibraryPersistenceLimits limits;
    limits.outstanding_receipts = 2;
    start({}, {}, limits);
    const auto first = writer->accept(revision(1));
    ASSERT_TRUE(first);
    library::CommitResult second;
    first.receipt->observe([&](auto event) {
        if (event.durable)
            second = writer->accept(revision(2));
    });
    ASSERT_TRUE(writer->flush().succeeded());
    ASSERT_TRUE(second);
    EXPECT_TRUE(second.receipt->durable());
    EXPECT_EQ(writer->durable_revision(), 2U);
}

TEST_F(LibraryPersistenceCore, DisposalReturnsTheFinalWriteOutcomeAfterACallbackAdmitsAnotherEdit)
{
    start();
    const auto first = writer->accept(revision(1));
    ASSERT_TRUE(first);
    library::CommitResult second;
    first.receipt->observe([&](auto event) {
        if (!event.durable)
            return;
        store.release({app::SnapshotWriteError::io});
        second = writer->accept(revision(2));
        writer.reset();
    });
    const auto result = writer->flush();
    EXPECT_EQ(result.error, app::SnapshotWriteError::io);
    EXPECT_FALSE(writer);
    ASSERT_TRUE(second);
    EXPECT_TRUE(first.receipt->durable());
    EXPECT_FALSE(second.receipt->durable());
    int late = 0;
    second.receipt->observe([&](auto) { ++late; });
    EXPECT_EQ(late, 0);
}

TEST_F(LibraryPersistenceCore, EarlierAcknowledgmentRetainsTheWaiterForALaterRevision)
{
    store.block();
    start();
    ASSERT_TRUE(writer->accept(revision(1)));
    ASSERT_TRUE(store.wait_entered());
    ASSERT_TRUE(writer->accept(revision(2)));
    std::vector<std::uint64_t> received;
    writer->when_durable(1, [&](bool saved) {
        EXPECT_TRUE(saved);
        received.push_back(1);
    });
    writer->when_durable(2, [&](bool saved) {
        EXPECT_TRUE(saved);
        received.push_back(2);
    });
    store.release();
    ASSERT_TRUE(writer->flush().succeeded());
    EXPECT_EQ(received, (std::vector<std::uint64_t>{1, 2}));
}

} // namespace
