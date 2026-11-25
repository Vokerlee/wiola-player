/**
 * @file
 * @brief Persistence admission, retained capacity and allocation recovery tests.
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

#include "fixture.hpp"
#include <limits>
#include <new>

namespace wiola::app::test::persistence {

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

#if WIOLA_TEST_ALLOCATOR_REPLACEMENT
TEST_F(LibraryPersistenceCore, EveryAdmissionAllocationFailurePreservesThePendingBoundary)
{
    bool observed_failure = false;
    for (std::size_t after = 0;; ++after) {
        const app::LibraryPersistenceLimits limits{
            .pending_revisions = 2,
            .outstanding_receipts = 3,
        };
        store.block();
        start({}, {}, limits);
        const auto first = writer->accept(revision(1, "first"));
        ASSERT_TRUE(first);
        ASSERT_TRUE(store.wait_entered());
        const auto delta = revision(2, "second");
        library::CommitResult accepted;
        bool failed = false;
        {
            const wiola::testing::AllocationFailure failure{after};
            try {
                accepted = writer->accept(delta);
            } catch (const std::bad_alloc&) {
                failed = true;
            }
        }
        EXPECT_EQ(writer->pending_revision(), failed ? 1U : 2U);
        EXPECT_EQ(writer->durable_revision(), 0U);
        if (failed) {
            observed_failure = true;
            EXPECT_FALSE(accepted);
            accepted = writer->accept(delta);
        }
        ASSERT_TRUE(accepted);
        EXPECT_EQ(accepted.revision, delta->changes.revision);
        EXPECT_FALSE(writer->accept(revision(3, "capacity-reserved")));

        store.release();
        ASSERT_TRUE(writer->flush().succeeded());
        EXPECT_TRUE(first.receipt->durable());
        EXPECT_TRUE(accepted.receipt->durable());
        EXPECT_EQ(writer->durable_revision(), delta->changes.revision);
        const auto snapshot = store.last_snapshot();
        ASSERT_EQ(snapshot.tracks.size(), 2U);
        EXPECT_EQ(snapshot.tracks.front().id.value, "first");
        EXPECT_EQ(snapshot.tracks.back(), delta->tracks.front());
        if (!failed) {
            EXPECT_TRUE(observed_failure);
            break;
        }
    }
}
#endif

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

} // namespace wiola::app::test::persistence
