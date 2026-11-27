/**
 * @file
 * @brief Portable projection scheduling, lifetime and failure contracts.
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

#include <wiola/presentation/tracks/track_projection.hpp>

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <stop_token>
#include <thread>
#include <utility>
#include <vector>

namespace {

using namespace wiola;
using namespace std::chrono_literals;

class ManualDelivery final {
public:
    enum class Posting {
        accept,
        reject,
        fail,
    };

    gui::TrackProjection::Delivery callbacks()
    {
        return {[this](auto task) {
                    std::lock_guard lock{mutex_};

                    ++attempts_;

                    if (posting_ != Posting::accept) {
                        changed_.notify_all();

                        if (posting_ == Posting::fail)
                            throw std::logic_error{"receiver unavailable"};

                        return false;
                    }

                    pending_.push_back(std::move(task));
                    changed_.notify_all();

                    return true;
                },
            [this] { discard(); }};
    }

    void posting(Posting value)
    {
        std::lock_guard lock{mutex_};
        posting_ = value;
    }

    bool wait_pending()
    {
        std::unique_lock lock{mutex_};
        return changed_.wait_for(lock, 5s, [this] { return !pending_.empty(); });
    }

    bool wait_attempts(std::size_t count)
    {
        std::unique_lock lock{mutex_};
        return changed_.wait_for(lock, 5s, [&] { return attempts_ >= count; });
    }

    std::vector<std::function<void()>> take()
    {
        std::lock_guard lock{mutex_};
        return std::exchange(pending_, {});
    }

    void drain()
    {
        for (auto& task : take())
            task();
    }

    void discard()
    {
        auto pending = take();
        if (fail_discard)
            throw std::logic_error{"receiver cleanup failed"};
    }

    bool fail_discard{};

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    std::vector<std::function<void()>> pending_;
    std::size_t attempts_{};
    Posting posting_{Posting::accept};
};

class CancellationGate final {
public:
    void enter(std::stop_token stop, std::function<void()> on_cancel = {})
    {
        std::stop_callback wake{stop, [&] {
                                    if (on_cancel)
                                        on_cancel();
                                    changed_.notify_all();
                                }};
        std::unique_lock lock{mutex_};

        entered_ = true;
        changed_.notify_all();
        changed_.wait(lock, [&] { return stop.stop_requested(); });
    }

    bool wait()
    {
        std::unique_lock lock{mutex_};
        return changed_.wait_for(lock, 5s, [this] { return entered_; });
    }

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    bool entered_{};
};

std::shared_ptr<const gui::TrackProjectionInput> input()
{
    gui::TrackRow first;
    first.id = engine::QueueEntryId{1};
    first.media = {"first"};
    first.metadata.title = "Obsolete";

    gui::TrackRow last;
    last.id = engine::QueueEntryId{2};
    last.media = {"last"};
    last.metadata.title = "Latest";

    std::map<library::MediaId, library::SearchFields> search;
    library::TrackRecord record;
    record.id = last.media;
    record.embedded = last.metadata;
    search.emplace(last.media, library::search_fields_of(record));

    return std::make_shared<const gui::TrackProjectionInput>(std::vector{first, last},
        std::move(search));
}

TEST(TrackProjectionCore, RequiresAnObserverAndBothDeliveryOperations)
{
    EXPECT_THROW((gui::TrackProjection{
                     {{}, [] { }},
                     [](auto) { }
    }),
        std::invalid_argument);
    EXPECT_THROW((gui::TrackProjection{
                     {[](auto) { return true; }, {}},
                     [](auto) { }
    }),
        std::invalid_argument);
    EXPECT_THROW((gui::TrackProjection{
                     {[](auto) { return true; }, [] { }},
                     {}
    }),
        std::invalid_argument);
}

TEST(TrackProjectionCore, NullRequestsAndClosedAdmissionNeverProduceWork)
{
    ManualDelivery delivery;
    int calls{};
    gui::TrackProjection projection{delivery.callbacks(), [&](auto) { ++calls; }};

    EXPECT_EQ(projection.request({}, {}), 0U);

    projection.stop();
    EXPECT_EQ(projection.request(input(), {}), 0U);

    projection.cancel();
    projection.stop();
    delivery.drain();
    EXPECT_EQ(calls, 0);
}

TEST(TrackProjectionCore, LatestGenerationReplacesBlockedWorkWithOwnedSearchFacts)
{
    ManualDelivery delivery;
    CancellationGate gate;
    std::vector<gui::TrackProjectionResult> results;
    gui::TrackProjection projection{delivery.callbacks(),
        [&](auto result) { results.push_back(std::move(result)); },
        [&](auto generation, auto stop) {
            if (generation == 1)
                gate.enter(stop);
        }};
    auto snapshot = input();
    gui::TrackViewState old;
    old.query.title = "Obsolete";

    const auto first = projection.request(snapshot, old);

    ASSERT_TRUE(gate.wait());

    gui::TrackViewState current;
    current.query.title = "Latest";

    const auto last = projection.request(snapshot, current);

    snapshot.reset();
    ASSERT_TRUE(delivery.wait_pending());

    delivery.drain();

    ASSERT_EQ(results.size(), 1U);
    EXPECT_NE(first, last);
    EXPECT_EQ(results.front().generation, last);
    EXPECT_FALSE(results.front().failure);
    ASSERT_EQ(results.front().rows.size(), 1U);
    EXPECT_EQ(results.front().rows.front().metadata.title, "Latest");
}

TEST(TrackProjectionCore, CallbackIdentitySurvivesMultipleOwnerDeliveries)
{
    ManualDelivery delivery;
    std::vector<int> calls;
    gui::TrackProjection projection{delivery.callbacks(),
        [&, count = 0](auto) mutable { calls.push_back(++count); }};

    for (int expected = 1; expected <= 2; ++expected) {
        static_cast<void>(projection.request(input(), {}));
        ASSERT_TRUE(delivery.wait_pending());

        delivery.drain();
        ASSERT_EQ(calls.size(), static_cast<std::size_t>(expected));
    }

    EXPECT_EQ(calls, (std::vector{1, 2}));
}

TEST(TrackProjectionCore, ObserverMayDisposeTheFacadeDuringDelivery)
{
    ManualDelivery delivery;
    int calls{};
    std::unique_ptr<gui::TrackProjection> projection;
    projection = std::make_unique<gui::TrackProjection>(delivery.callbacks(), [&](auto) {
        ++calls;
        projection.reset();
    });
    static_cast<void>(projection->request(input(), {}));
    ASSERT_TRUE(delivery.wait_pending());

    delivery.drain();
    EXPECT_FALSE(projection);
    EXPECT_EQ(calls, 1);
}

TEST(TrackProjectionCore, ADetachedDeliveryCannotKeepTheWorkflowOrInputAlive)
{
    ManualDelivery delivery;
    int calls{};
    auto projection =
        std::make_unique<gui::TrackProjection>(delivery.callbacks(), [&](auto) { ++calls; });
    auto snapshot = input();
    const std::weak_ptr<const gui::TrackProjectionInput> released = snapshot;

    static_cast<void>(projection->request(snapshot, {}));
    snapshot.reset();
    ASSERT_TRUE(delivery.wait_pending());

    auto posted = delivery.take();

    projection.reset();
    EXPECT_TRUE(released.expired());

    for (auto& task : posted)
        task();

    EXPECT_EQ(calls, 0);
}

TEST(TrackProjectionCore, AComputationExceptionIsOwnedByItsResultAndTheWorkerRemainsUsable)
{
    ManualDelivery delivery;
    std::vector<gui::TrackProjectionResult> results;
    gui::TrackProjection projection{delivery.callbacks(),
        [&](auto result) { results.push_back(std::move(result)); },
        [calls = 0](auto, auto) mutable {
            if (++calls == 1)
                throw std::logic_error{"computation failed"};
        }};

    for (int expected = 1; expected <= 2; ++expected) {
        static_cast<void>(projection.request(input(), {}));
        ASSERT_TRUE(delivery.wait_pending());

        delivery.drain();
        ASSERT_EQ(results.size(), static_cast<std::size_t>(expected));
    }

    ASSERT_TRUE(results.front().failure);
    EXPECT_THROW(std::rethrow_exception(results.front().failure), std::logic_error);
    EXPECT_FALSE(results.back().failure);
    EXPECT_EQ(results.back().rows.size(), input()->rows().size());
}

TEST(TrackProjectionCore, FailedOrRejectedPostingCannotTerminateTheWorkerOrKeepPostingLatched)
{
    for (const auto mode : {ManualDelivery::Posting::reject, ManualDelivery::Posting::fail}) {
        ManualDelivery delivery;
        delivery.posting(mode);

        std::vector<gui::TrackProjectionResult> results;
        gui::TrackProjection projection{delivery.callbacks(),
            [&](auto result) { results.push_back(std::move(result)); }};
        const auto first = projection.request(input(), {});

        ASSERT_TRUE(delivery.wait_attempts(1));

        delivery.posting(ManualDelivery::Posting::accept);

        const auto next = projection.request(input(), {});

        ASSERT_TRUE(delivery.wait_pending());

        delivery.drain();
        ASSERT_EQ(results.size(), 1U);
        EXPECT_NE(next, first);
        EXPECT_EQ(results.front().generation, next);
    }
}

TEST(TrackProjectionCore, OwnerThreadRulesApplyBeforeAnyStateMutation)
{
    ManualDelivery delivery;
    gui::TrackProjection projection{delivery.callbacks(), [](auto) { }};
    std::array<bool, 3> rejected{};
    std::thread other{[&] {
        try {
            static_cast<void>(projection.request(input(), {}));
        } catch (const std::logic_error&) {
            rejected[0] = true;
        }

        try {
            projection.cancel();
        } catch (const std::logic_error&) {
            rejected[1] = true;
        }

        try {
            projection.stop();
        } catch (const std::logic_error&) {
            rejected[2] = true;
        }
    }};
    other.join();
    EXPECT_EQ(rejected, (std::array{true, true, true}));
    EXPECT_NE(projection.request(input(), {}), 0U);
}

TEST(TrackProjectionCore, ObserverFailureConsumesOnlyItsOwnResultAndKeepsCallbackIdentity)
{
    ManualDelivery delivery;
    std::vector<int> calls;
    gui::TrackProjection projection{delivery.callbacks(), [&, count = 0](auto) mutable {
                                        calls.push_back(++count);
                                        if (count == 1)
                                            throw std::logic_error{"observer failed"};
                                    }};
    static_cast<void>(projection.request(input(), {}));
    ASSERT_TRUE(delivery.wait_pending());
    EXPECT_THROW(delivery.drain(), std::logic_error);

    static_cast<void>(projection.request(input(), {}));
    ASSERT_TRUE(delivery.wait_pending());
    EXPECT_NO_THROW(delivery.drain());
    EXPECT_EQ(calls, (std::vector{1, 2}));
}

TEST(TrackProjectionCore, StopJoinsTheWorkerBeforeReportingAReceiverCleanupFailure)
{
    ManualDelivery delivery;
    delivery.fail_discard = true;

    CancellationGate gate;
    gui::TrackProjection projection{delivery.callbacks(), [](auto) { },
        [&](auto, auto stop) { gate.enter(stop); }};
    auto snapshot = input();
    std::weak_ptr<const gui::TrackProjectionInput> released = snapshot;

    static_cast<void>(projection.request(snapshot, {}));
    snapshot.reset();
    ASSERT_TRUE(gate.wait());
    EXPECT_THROW(projection.stop(), std::logic_error);
    EXPECT_TRUE(released.expired());
    EXPECT_EQ(projection.request(input(), {}), 0U);
}

TEST(TrackProjectionCore, DestructionFinishesTheWorkerEvenWhenReceiverCleanupFails)
{
    ManualDelivery delivery;
    delivery.fail_discard = true;

    CancellationGate gate;
    auto projection = std::make_unique<gui::TrackProjection>(
        delivery.callbacks(), [](auto) { }, [&](auto, auto stop) { gate.enter(stop); }
    );
    auto snapshot = input();
    std::weak_ptr<const gui::TrackProjectionInput> released = snapshot;

    static_cast<void>(projection->request(snapshot, {}));
    snapshot.reset();
    ASSERT_TRUE(gate.wait());

    projection.reset();
    EXPECT_TRUE(released.expired());
}

TEST(TrackProjectionCore, CancellationCallbacksMayReenterTheOwnerWithoutHoldingTheStateLock)
{
    ManualDelivery delivery;
    CancellationGate gate;
    gui::TrackProjection* owner{};
    bool reentered{};
    std::vector<gui::TrackProjectionResult> results;
    gui::TrackProjection projection{delivery.callbacks(),
        [&](auto result) { results.push_back(std::move(result)); },
        [&](auto generation, auto stop) {
            if (generation == 1)
                gate.enter(stop, [&] {
                    reentered = true;
                    owner->cancel();
                });
        }};
    owner = &projection;
    static_cast<void>(projection.request(input(), {}));
    ASSERT_TRUE(gate.wait());

    static_cast<void>(projection.request(input(), {}));
    EXPECT_TRUE(reentered);

    const auto final = projection.request(input(), {});

    ASSERT_TRUE(delivery.wait_pending());

    delivery.drain();

    // The reentrant cancel removed the intermediate query; only the final one remains eligible.
    if (results.empty()) {
        ASSERT_TRUE(delivery.wait_pending());
        delivery.drain();
    }

    ASSERT_EQ(results.size(), 1U);
    EXPECT_EQ(results.front().generation, final);
}

TEST(TrackProjectionCore, CompletedPayloadsCoalesceBehindOneQueuedOwnerDelivery)
{
    ManualDelivery delivery;
    std::mutex mutex;
    std::condition_variable changed;
    bool completed{};
    std::vector<gui::TrackProjectionResult> results;
    gui::TrackProjection projection{delivery.callbacks(),
        [&](auto result) { results.push_back(std::move(result)); }};
    static_cast<void>(projection.request(input(), {}));
    ASSERT_TRUE(delivery.wait_pending());

    auto next = std::shared_ptr<const gui::TrackProjectionInput>{new gui::TrackProjectionInput{},
        [&](const auto* snapshot) {
            delete snapshot;

            std::lock_guard lock{mutex};

            completed = true;
            changed.notify_all();
        }};
    const auto final = projection.request(next, {});

    next.reset();
    {
        std::unique_lock lock{mutex};
        ASSERT_TRUE(changed.wait_for(lock, 5s, [&] { return completed; }));
    }

    auto pending = delivery.take();

    ASSERT_EQ(pending.size(), 1U);

    pending.front()();
    ASSERT_EQ(results.size(), 1U);
    EXPECT_EQ(results.front().generation, final);
    EXPECT_TRUE(results.front().rows.empty());
}

TEST(TrackProjectionCore, ReceiverCleanupMayDisposeTheFacadeBeforeItsWorkerHasJoined)
{
    ManualDelivery delivery;
    CancellationGate gate;
    bool may_dispose{};
    std::unique_ptr<gui::TrackProjection> projection;
    auto callbacks = delivery.callbacks();
    callbacks.discard = [&] {
        if (may_dispose)
            projection.reset();
        delivery.discard();
    };
    projection = std::make_unique<gui::TrackProjection>(
        std::move(callbacks), [](auto) { }, [&](auto, auto stop) { gate.enter(stop); }
    );

    auto snapshot = input();
    std::weak_ptr<const gui::TrackProjectionInput> released = snapshot;

    static_cast<void>(projection->request(snapshot, {}));
    snapshot.reset();
    ASSERT_TRUE(gate.wait());

    auto* owner = projection.get();

    may_dispose = true;
    owner->stop();
    EXPECT_FALSE(projection);
    EXPECT_TRUE(released.expired());
}

TEST(TrackProjectionCore, CancellationCallbacksCanCloseWithoutJoiningTheirOwnCallbackFrame)
{
    ManualDelivery delivery;
    CancellationGate gate;
    gui::TrackProjection* owner{};
    gui::TrackProjection projection{delivery.callbacks(), [](auto) { },
        [&](auto generation, auto stop) {
            if (generation == 1)
                gate.enter(stop, [&] { owner->stop(); });
        }};
    owner = &projection;
    static_cast<void>(projection.request(input(), {}));
    ASSERT_TRUE(gate.wait());

    EXPECT_NE(projection.request(input(), {}), 0U);
    EXPECT_EQ(projection.request(input(), {}), 0U);
}

TEST(TrackProjectionCore, CancellationMayDisposeTheFacadeWhileAnOwnerRequestRetainsItsState)
{
    ManualDelivery delivery;
    CancellationGate gate;
    bool may_dispose{};
    std::unique_ptr<gui::TrackProjection> projection;
    projection = std::make_unique<gui::TrackProjection>(
        delivery.callbacks(), [](auto) { },
        [&](auto generation, auto stop) {
            if (generation == 1)
                gate.enter(stop, [&] {
                    if (may_dispose)
                        projection.reset();
                });
        }
    );

    auto snapshot = input();
    const std::weak_ptr<const gui::TrackProjectionInput> released = snapshot;

    static_cast<void>(projection->request(snapshot, {}));
    snapshot.reset();
    ASSERT_TRUE(gate.wait());

    auto* owner = projection.get();

    may_dispose = true;

    EXPECT_NE(owner->request(input(), {}), 0U);
    EXPECT_FALSE(projection);
    EXPECT_TRUE(released.expired());
}

TEST(TrackProjectionCore, CancelClearsAQueuedCompletionWithoutClosingAdmission)
{
    ManualDelivery delivery;
    std::vector<gui::TrackProjectionResult> results;
    gui::TrackProjection projection{delivery.callbacks(),
        [&](auto result) { results.push_back(std::move(result)); }};
    static_cast<void>(projection.request(input(), {}));
    ASSERT_TRUE(delivery.wait_pending());

    projection.cancel();
    delivery.drain();
    EXPECT_TRUE(results.empty());

    const auto next = projection.request(input(), {});

    ASSERT_TRUE(delivery.wait_pending());

    delivery.drain();
    ASSERT_EQ(results.size(), 1U);
    EXPECT_EQ(results.front().generation, next);
}

} // namespace
