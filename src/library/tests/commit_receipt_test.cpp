/**
 * @file
 * @brief Owner-thread durability receipt lifecycle and failure-isolation tests.
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
#include <thread>
#include <type_traits>
#include <vector>
#include <wiola/library/commit_receipt.hpp>

namespace {

using namespace wiola::library;

static_assert(!std::is_copy_constructible_v<CommitReceipt>);
static_assert(!std::is_move_constructible_v<CommitReceipt>);

TEST(CommitReceipt, WaitsForAnAttemptAndIgnoresEmptyObservers)
{
    CommitReceipt receipt{17};
    receipt.observe({});
    std::vector<bool> events;
    receipt.observe([&](auto event) {
        EXPECT_EQ(event.revision, 17U);
        events.push_back(event.durable);
    });
    EXPECT_FALSE(receipt.durable());
    EXPECT_TRUE(events.empty());
    receipt.notify(false);
    receipt.notify(false);
    EXPECT_EQ(events, (std::vector<bool>{false, false}));
    EXPECT_FALSE(receipt.durable());
}

TEST(CommitReceipt, RetainsFailedObserversForRetryAndInformsLateSubscribers)
{
    CommitReceipt receipt{23};
    receipt.notify(false);
    std::vector<bool> events;
    receipt.observe([&](auto event) {
        EXPECT_EQ(event.revision, 23U);
        events.push_back(event.durable);
    });
    EXPECT_EQ(events, std::vector<bool>{false});
    receipt.notify(true);
    EXPECT_EQ(events, (std::vector<bool>{false, true}));
    EXPECT_TRUE(receipt.durable());
}

TEST(CommitReceipt, DurabilityIsTerminalAndLateObserversReceiveTheExactRevision)
{
    CommitReceipt receipt{29};
    std::vector<bool> events;
    receipt.observe([&](auto event) { events.push_back(event.durable); });
    receipt.notify(true);
    receipt.notify(false);
    receipt.notify(true);
    receipt.observe([&](auto event) {
        EXPECT_EQ(event.revision, 29U);
        events.push_back(event.durable);
    });
    EXPECT_EQ(events, (std::vector<bool>{true, true}));
    EXPECT_TRUE(receipt.durable());
}

TEST(CommitReceipt, CancellationDiscardsPendingAndFutureObservers)
{
    CommitReceipt receipt{31};
    int calls = 0;
    receipt.observe([&](auto) { ++calls; });
    receipt.cancel();
    receipt.cancel();
    receipt.notify(false);
    receipt.notify(true);
    receipt.observe([&](auto) { ++calls; });
    EXPECT_EQ(calls, 0);
    EXPECT_FALSE(receipt.durable());
}

TEST(CommitReceipt, CancellationDuringDeliveryStopsRemainingObservers)
{
    for (const bool durable : {false, true}) {
        CommitReceipt receipt{37};
        int first = 0, second = 0;
        receipt.observe([&](auto) {
            ++first;
            receipt.cancel();
        });
        receipt.observe([&](auto) { ++second; });
        receipt.notify(durable);
        receipt.observe([&](auto) { ++second; });
        receipt.notify(true);
        EXPECT_EQ(first, 1);
        EXPECT_EQ(second, 0);
        EXPECT_EQ(receipt.durable(), durable);
    }
}

TEST(CommitReceipt, ReentrantSuccessStopsTheSupersededFailureDelivery)
{
    CommitReceipt receipt{41};
    std::vector<int> events;
    receipt.observe([&](auto event) {
        events.push_back(event.durable ? 2 : 1);
        if (!event.durable)
            receipt.notify(true);
    });
    receipt.observe([&](auto event) { events.push_back(event.durable ? 3 : 4); });
    receipt.notify(false);
    EXPECT_EQ(events, (std::vector{1, 2, 3}));
    EXPECT_TRUE(receipt.durable());
}

TEST(CommitReceipt, ObserversAddedDuringDeliveryReceiveTheCurrentStateOnce)
{
    for (const bool durable : {false, true}) {
        CommitReceipt receipt{43};
        int added_calls = 0;
        receipt.observe([&](auto event) {
            receipt.observe([&](auto added) {
                EXPECT_EQ(added.revision, 43U);
                EXPECT_EQ(added.durable, durable);
                ++added_calls;
            });
            EXPECT_EQ(event.durable, durable);
        });
        receipt.notify(durable);
        EXPECT_EQ(added_calls, 1);
    }
}

TEST(CommitReceipt, ObserverExceptionsDoNotStrandOtherObservers)
{
    for (const bool durable : {false, true}) {
        CommitReceipt receipt{47};
        std::vector<int> calls;
        receipt.observe([&](auto) {
            calls.push_back(1);
            throw std::runtime_error{"first observer"};
        });
        receipt.observe([&](auto) {
            calls.push_back(2);
            throw std::logic_error{"second observer"};
        });
        receipt.observe([&](auto event) {
            EXPECT_EQ(event.revision, 47U);
            EXPECT_EQ(event.durable, durable);
            calls.push_back(3);
        });
        try {
            receipt.notify(durable);
            FAIL() << "the first observer exception must reach the caller";
        } catch (const std::runtime_error& error) {
            EXPECT_STREQ(error.what(), "first observer");
        }
        EXPECT_EQ(calls, (std::vector{1, 2, 3}));
        EXPECT_EQ(receipt.durable(), durable);
        receipt.cancel();
    }
}

TEST(CommitReceipt, FailureObserverExceptionsDoNotPreventALaterSuccessfulRetry)
{
    CommitReceipt receipt{53};
    std::vector<bool> events;
    receipt.observe([](auto event) {
        if (!event.durable)
            throw std::runtime_error{"failed attempt observer"};
    });
    receipt.observe([&](auto event) { events.push_back(event.durable); });
    EXPECT_THROW(receipt.notify(false), std::runtime_error);
    receipt.notify(true);
    EXPECT_EQ(events, (std::vector<bool>{false, true}));
    EXPECT_TRUE(receipt.durable());
}

TEST(CommitReceipt, RejectsCrossThreadMutationWithoutChangingOwnerState)
{
    CommitReceipt receipt{59};
    int rejected = 0;
    std::thread other{[&] {
        try {
            receipt.observe([](auto) { });
        } catch (const std::logic_error&) {
            ++rejected;
        }
        try {
            receipt.notify(true);
        } catch (const std::logic_error&) {
            ++rejected;
        }
        try {
            receipt.cancel();
        } catch (const std::logic_error&) {
            ++rejected;
        }
    }};
    other.join();
    EXPECT_EQ(rejected, 3);
    EXPECT_FALSE(receipt.durable());
    int calls = 0;
    receipt.observe([&](auto) { ++calls; });
    receipt.notify(true);
    EXPECT_EQ(calls, 1);
}

TEST(CommitResult, DistinguishesRejectedAcceptedAndSynchronouslyDurableResults)
{
    const CommitResult empty;
    const CommitResult rejected{false};
    EXPECT_FALSE(empty);
    EXPECT_FALSE(rejected);
    EXPECT_FALSE(rejected.durable());
    const CommitResult saved{true};
    EXPECT_TRUE(saved);
    EXPECT_TRUE(saved.durable());
    auto receipt = std::make_shared<CommitReceipt>(61);
    const auto accepted = CommitResult::accepted(61, receipt);
    EXPECT_TRUE(accepted);
    EXPECT_FALSE(accepted.durable());
    EXPECT_EQ(accepted.revision, 61U);
    EXPECT_EQ(accepted.receipt, receipt);
    receipt->notify(true);
    EXPECT_TRUE(accepted.receipt->durable());
    EXPECT_FALSE(accepted.durable()); // The original result records acceptance, not later delivery.
}

TEST(CommitReceipt, MutableObserverStateSurvivesFailedAttemptsAndSuccess)
{
    CommitReceipt receipt{67};
    std::vector<int> calls;
    receipt.observe([count = 0, &calls](auto) mutable { calls.push_back(++count); });
    receipt.notify(false);
    receipt.notify(false);
    receipt.notify(true);
    EXPECT_EQ(calls, (std::vector{1, 2, 3}));
}

TEST(CommitReceipt, CallbackCanReleaseTheLastOwningHandleAndCancelRemainingDelivery)
{
    for (const bool durable : {false, true}) {
        auto receipt = std::make_shared<CommitReceipt>(71);
        const std::weak_ptr<CommitReceipt> weak = receipt;
        int first = 0, remaining = 0;
        receipt->observe([&](auto) {
            ++first;
            receipt.reset();
        });
        receipt->observe([&](auto) { ++remaining; });
        receipt->notify(durable);
        EXPECT_TRUE(weak.expired());
        EXPECT_EQ(first, 1);
        EXPECT_EQ(remaining, 0);
    }
}

TEST(CommitReceipt, LateFailedObserverCanReleaseTheLastOwningHandle)
{
    auto receipt = std::make_shared<CommitReceipt>(73);
    receipt->notify(false);
    const std::weak_ptr<CommitReceipt> weak = receipt;
    int calls = 0;
    receipt->observe([&](auto event) {
        EXPECT_FALSE(event.durable);
        ++calls;
        receipt.reset();
    });
    EXPECT_TRUE(weak.expired());
    EXPECT_EQ(calls, 1);
}

TEST(CommitReceipt, RethrowsObserverFailureAfterTheOwningHandleIsReleased)
{
    auto receipt = std::make_shared<CommitReceipt>(79);
    int remaining = 0;
    receipt->observe([&](auto) {
        receipt.reset();
        throw std::runtime_error{"released receipt observer"};
    });
    receipt->observe([&](auto) { ++remaining; });
    EXPECT_THROW(receipt->notify(true), std::runtime_error);
    EXPECT_FALSE(receipt);
    EXPECT_EQ(remaining, 0);
}

TEST(CommitReceipt, CancellationCanReleaseASelfCapturingObserver)
{
    auto receipt = std::make_shared<CommitReceipt>(83);
    const std::weak_ptr<CommitReceipt> weak = receipt;
    auto* retained = receipt.get();
    receipt->observe([owner = receipt](auto) { static_cast<void>(owner->durable()); });
    receipt.reset();
    ASSERT_FALSE(weak.expired());
    retained->cancel();
    EXPECT_TRUE(weak.expired());
}

} // namespace
