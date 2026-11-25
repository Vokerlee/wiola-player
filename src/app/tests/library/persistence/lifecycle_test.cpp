/**
 * @file
 * @brief Persistence ownership, shutdown and reentrant disposal tests.
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
#include <stdexcept>
#include <thread>
#include <type_traits>

namespace wiola::app::test::persistence {

static_assert(!std::is_copy_constructible_v<app::LibraryPersistence>);
static_assert(!std::is_move_constructible_v<app::LibraryPersistence>);

TEST_F(LibraryPersistenceCore, CleanupDuringDisposalRejectsReentrantAdmissionAndDurabilityWaiters)
{
    start();
    const auto accepted = writer->accept(revision(1));
    ASSERT_TRUE(accepted);
    bool destroyed = false, admitted = true;
    int calls = 0;

    struct Cleanup {
        app::LibraryPersistence* writer;
        std::shared_ptr<const library::RevisionDelta> delta;
        bool& destroyed;
        bool& admitted;
        int& calls;

        ~Cleanup()
        {
            destroyed = true;
            admitted = static_cast<bool>(writer->accept(delta));
            writer->when_durable(0, [&notifications = calls](bool) { ++notifications; });
        }
    };

    auto cleanup = std::make_shared<Cleanup>(writer.get(), revision(2), destroyed, admitted, calls);
    accepted.receipt->observe([cleanup](auto) { });
    cleanup.reset();

    writer.reset();
    delivery.drain();

    EXPECT_TRUE(destroyed);
    EXPECT_FALSE(admitted);
    EXPECT_EQ(calls, 0);
}

TEST_F(LibraryPersistenceCore, RetryAfterStopPreservesTheFinalWriteWithoutAnotherNotification)
{
    int saved = 0;
    start({}, [&](auto, auto) { ++saved; });
    const auto accepted = writer->accept(revision(1));
    ASSERT_TRUE(accepted);
    ASSERT_TRUE(writer->flush().succeeded());
    const auto snapshot = store.last_snapshot();
    writer->stop();

    writer->retry();

    EXPECT_TRUE(writer->flush().succeeded());
    EXPECT_TRUE(accepted.receipt->durable());
    EXPECT_EQ(saved, 1);
    const auto saved_snapshot = store.last_snapshot();
    EXPECT_EQ(saved_snapshot.tracks, snapshot.tracks);
    EXPECT_EQ(saved_snapshot.playlists, snapshot.playlists);
    EXPECT_EQ(saved_snapshot.workspaces, snapshot.workspaces);
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

} // namespace wiola::app::test::persistence
