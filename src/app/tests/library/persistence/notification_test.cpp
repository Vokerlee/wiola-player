/**
 * @file
 * @brief Persistence receipts, owner delivery and observer failure tests.
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

#include <new>
#include <stdexcept>

namespace wiola::app::test::persistence {

TEST_F(LibraryPersistenceCore, RejectedOrThrowingDeliveryStillAcknowledgesFlushAndStopExactlyOnce)
{
    for (const bool throwing : {false, true}) {
        for (const bool closing : {false, true}) {
            std::vector<std::uint64_t> saved;
            auto callbacks = delivery.callbacks();
            callbacks.post = [throwing](auto) -> bool {
                if (throwing)
                    throw std::runtime_error{"delivery unavailable"};
                return false;
            };
            writer = std::make_unique<app::LibraryPersistence>(store, std::move(callbacks),
                library::LibrarySnapshot{}, [&](auto number, auto result) {
                    EXPECT_TRUE(result.succeeded());
                    saved.push_back(number);
                });

            const auto accepted = writer->accept(revision(1));

            ASSERT_TRUE(accepted);

            if (closing)
                writer->stop();
            else
                EXPECT_TRUE(writer->flush().succeeded());

            EXPECT_TRUE(accepted.receipt->durable());
            EXPECT_EQ(saved, (std::vector<std::uint64_t>{1}));
            EXPECT_TRUE(writer->flush().succeeded());

            writer->stop();
            EXPECT_EQ(saved, (std::vector<std::uint64_t>{1}));

            writer.reset();
        }
    }
}

TEST_F(LibraryPersistenceCore,
    FailedDrainAcknowledgesTheAttemptBeforeRethrowingAndIgnoresItsLateTask)
{
    auto callbacks = delivery.callbacks();
    callbacks.drain = [] {
        throw std::runtime_error{"drain failed"};
    };

    int saved = 0;
    writer = std::make_unique<app::LibraryPersistence>(store, std::move(callbacks),
        library::LibrarySnapshot{}, [&](auto, auto) { ++saved; });

    const auto accepted = writer->accept(revision(1));

    ASSERT_TRUE(accepted);

    EXPECT_THROW(writer->flush(), std::runtime_error);

    EXPECT_TRUE(accepted.receipt->durable());
    EXPECT_EQ(saved, 1);

    delivery.drain();
    EXPECT_EQ(saved, 1);
    EXPECT_THROW(writer->stop(), std::runtime_error);
    EXPECT_EQ(saved, 1);
}

TEST_F(LibraryPersistenceCore, EarlierObserverFailureCannotStrandTheLatestWrittenAttempt)
{
    store.block();

    int saved = 0;
    start({}, [&](auto, auto) { ++saved; });

    const auto first = writer->accept(revision(1, "first"));

    ASSERT_TRUE(first);
    ASSERT_TRUE(store.wait_entered());

    const auto second = writer->accept(revision(2, "second"));

    ASSERT_TRUE(second);

    first.receipt->observe([](auto) { throw std::runtime_error{"first receipt"}; });
    store.release();
    ASSERT_TRUE(delivery.wait_pending(2));

    EXPECT_THROW(writer->flush(), std::runtime_error);

    EXPECT_TRUE(first.receipt->durable());
    EXPECT_TRUE(second.receipt->durable());
    EXPECT_EQ(saved, 2);

    delivery.drain();
    EXPECT_EQ(saved, 2);
}

TEST_F(LibraryPersistenceCore,
    FallbackObserverFailureIsRethrownAfterAcknowledgmentWithoutRepeatingIt)
{
    auto callbacks = delivery.callbacks();
    callbacks.post = [](auto) {
        return false;
    };

    int saved = 0;
    writer = std::make_unique<app::LibraryPersistence>(store, std::move(callbacks),
        library::LibrarySnapshot{}, [&](auto, auto) {
            ++saved;
            throw std::logic_error{"save observer"};
        });

    const auto accepted = writer->accept(revision(1));

    ASSERT_TRUE(accepted);

    EXPECT_THROW(writer->flush(), std::logic_error);

    EXPECT_TRUE(accepted.receipt->durable());
    EXPECT_EQ(saved, 1);
    EXPECT_TRUE(writer->flush().succeeded());

    writer->stop();
    EXPECT_EQ(saved, 1);
}

TEST_F(LibraryPersistenceCore, DrainAndFallbackFailuresPreserveTheFirstException)
{
    auto callbacks = delivery.callbacks();
    callbacks.post = [](auto) {
        return false;
    };
    callbacks.drain = [] {
        throw std::runtime_error{"drain failed"};
    };

    int saved = 0;
    writer = std::make_unique<app::LibraryPersistence>(store, std::move(callbacks),
        library::LibrarySnapshot{}, [&](auto, auto) {
            ++saved;
            throw std::logic_error{"save observer"};
        });

    const auto accepted = writer->accept(revision(1));

    ASSERT_TRUE(accepted);

    try {
        static_cast<void>(writer->flush());
        FAIL() << "drain failure was discarded";
    } catch (const std::runtime_error& error) {
        EXPECT_STREQ(error.what(), "drain failed");
    }

    EXPECT_TRUE(accepted.receipt->durable());
    EXPECT_EQ(saved, 1);
    EXPECT_THROW(writer->stop(), std::runtime_error);
    EXPECT_EQ(saved, 1);
}

TEST_F(LibraryPersistenceCore, FallbackCompletesAllObserversAndPreservesTheirFirstFailure)
{
    auto callbacks = delivery.callbacks();
    callbacks.post = [](auto) {
        return false;
    };

    int receipts = 0, waiters = 0, saves = 0;

    store.block();
    writer = std::make_unique<app::LibraryPersistence>(store, std::move(callbacks),
        library::LibrarySnapshot{}, [&](auto, auto) {
            ++saves;
            throw std::logic_error{"save observer"};
        });

    const auto first = writer->accept(revision(1, "first"));

    ASSERT_TRUE(first);
    ASSERT_TRUE(store.wait_entered());

    const auto second = writer->accept(revision(2, "second"));

    ASSERT_TRUE(second);

    first.receipt->observe([&](auto) {
        ++receipts;
        throw std::logic_error{"first receipt"};
    });
    first.receipt->observe([&](auto) {
        ++receipts;
        throw std::runtime_error{"later observer"};
    });
    second.receipt->observe([&](auto) {
        ++receipts;
        throw std::logic_error{"second receipt"};
    });
    writer->when_durable(2, [&](bool) {
        ++waiters;
        throw std::logic_error{"waiter"};
    });
    store.release();

    try {
        static_cast<void>(writer->flush());
        FAIL() << "first observer failure was discarded";
    } catch (const std::logic_error& error) {
        EXPECT_STREQ(error.what(), "first receipt");
    }

    EXPECT_TRUE(first.receipt->durable());
    EXPECT_TRUE(second.receipt->durable());
    EXPECT_EQ(receipts, 3);
    EXPECT_EQ(waiters, 1);
    EXPECT_EQ(saves, 1);
    EXPECT_TRUE(writer->flush().succeeded());

    writer->stop();
    EXPECT_EQ(receipts, 3);
    EXPECT_EQ(waiters, 1);
    EXPECT_EQ(saves, 1);
}

TEST_F(LibraryPersistenceCore, FallbackWaiterFailurePreservesTheAcknowledgment)
{
    auto callbacks = delivery.callbacks();
    callbacks.post = [](auto) {
        return false;
    };
    store.block();
    writer = std::make_unique<app::LibraryPersistence>(store, std::move(callbacks),
        library::LibrarySnapshot{});

    const auto accepted = writer->accept(revision(1));

    ASSERT_TRUE(accepted);
    ASSERT_TRUE(store.wait_entered());

    int calls = 0;
    writer->when_durable(1, [&](bool saved) {
        EXPECT_TRUE(saved);

        ++calls;
        throw std::logic_error{"waiter"};
    });
    store.release();

    EXPECT_THROW(writer->flush(), std::logic_error);

    EXPECT_TRUE(accepted.receipt->durable());
    EXPECT_EQ(calls, 1);
    EXPECT_NO_THROW(writer->stop());
    EXPECT_EQ(calls, 1);
}

#if WIOLA_TEST_ALLOCATOR_REPLACEMENT
TEST_F(LibraryPersistenceCore, EveryCompletionAllocationFailureRemainsRetryableAfterFlushOrStop)
{
    for (const bool closing : {false, true}) {
        bool observed_failure = false;
        for (std::size_t after = 0;; ++after) {
            int saved = 0, receipt_calls = 0;
            start({}, [&](auto, auto) { ++saved; });

            const auto accepted = writer->accept(revision(1));

            ASSERT_TRUE(accepted);

            accepted.receipt->observe([&](auto) { ++receipt_calls; });
            ASSERT_TRUE(delivery.wait_pending(1));

            bool failed = false;
            {
                const wiola::testing::AllocationFailure failure{after};
                try {
                    if (closing)
                        writer->stop();
                    else
                        static_cast<void>(writer->flush());
                } catch (const std::bad_alloc&) {
                    failed = true;
                }
            }
            EXPECT_EQ(writer->durable_revision(), accepted.revision);

            if (failed) {
                observed_failure = true;
                EXPECT_FALSE(accepted.receipt->durable());
                EXPECT_EQ(saved, 0);
                EXPECT_EQ(receipt_calls, 0);
            }

            EXPECT_TRUE(writer->flush().succeeded());
            EXPECT_TRUE(accepted.receipt->durable());
            EXPECT_EQ(saved, 1);
            EXPECT_EQ(receipt_calls, 1);

            writer->stop();
            EXPECT_EQ(saved, 1);
            EXPECT_EQ(receipt_calls, 1);

            writer.reset();

            if (!failed) {
                EXPECT_TRUE(observed_failure);
                break;
            }
        }
    }
}
#endif

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

TEST_F(LibraryPersistenceCore, ReorderedOldFailureCannotRegressASuccessfullyAcknowledgedPrefix)
{
    store.block();

    std::vector<app::SnapshotWriteResult> saved;
    start({}, [&](auto, auto result) { saved.push_back(result); });

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
    EXPECT_EQ(saved, (std::vector<app::SnapshotWriteResult>{{}}));
    EXPECT_TRUE(first.receipt->durable());
    EXPECT_TRUE(second.receipt->durable());
    EXPECT_EQ(writer->durable_revision(), 2U);
    EXPECT_EQ(store.last_snapshot().tracks.size(), 2U);
}

TEST_F(LibraryPersistenceCore, NewestFailedAttemptStillAcknowledgesTheEarlierDurablePrefix)
{
    std::vector<app::SnapshotWriteResult> saved;
    start({}, [&](auto, auto result) { saved.push_back(result); });

    const auto first = writer->accept(revision(1, "first"));

    ASSERT_TRUE(first);
    ASSERT_TRUE(delivery.wait_pending(1));

    store.release({app::SnapshotWriteError::io});

    const auto second = writer->accept(revision(2, "second"));

    ASSERT_TRUE(second);
    ASSERT_TRUE(delivery.wait_pending(2));

    delivery.reverse_pending();

    EXPECT_EQ(writer->flush().error, app::SnapshotWriteError::io);

    EXPECT_TRUE(first.receipt->durable());
    EXPECT_FALSE(second.receipt->durable());
    ASSERT_FALSE(saved.empty());
    EXPECT_TRUE(std::all_of(saved.begin(), saved.end(),
        [](auto result) { return result.error == app::SnapshotWriteError::io; }));

    const auto failed_attempts = saved.size();

    store.release();
    writer->retry();
    ASSERT_TRUE(writer->flush().succeeded());
    EXPECT_TRUE(second.receipt->durable());
    EXPECT_EQ(saved.size(), failed_attempts + 1);
    EXPECT_TRUE(saved.back().succeeded());
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

} // namespace wiola::app::test::persistence
