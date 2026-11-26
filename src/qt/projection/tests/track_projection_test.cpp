/**
 * @file
 * @brief Stable multi-column track sorting regression tests.
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

#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>
#include <chrono>
#include <condition_variable>
#include <gtest/gtest.h>
#include <mutex>
#include <qt/projection/track_projection.hpp>
#include <stdexcept>
#include <stop_token>
#include <thread>
#include <vector>

namespace {

using namespace wiola;

struct Gate {
    std::mutex mutex;
    std::condition_variable changed;
    bool entered{};

    void enter(std::stop_token stop)
    {
        std::stop_callback wake{stop, [this] { changed.notify_all(); }};
        std::unique_lock lock{mutex};
        entered = true;
        changed.notify_all();
        changed.wait(lock, [&] { return stop.stop_requested(); });
    }

    bool wait()
    {
        std::unique_lock lock{mutex};
        return changed.wait_for(lock, std::chrono::seconds{10}, [&] { return entered; });
    }
};

std::shared_ptr<const gui::TrackProjectionInput> input()
{
    std::vector<gui::TrackRow> rows;
    for (int i = 0; i < 3; ++i) {
        gui::TrackRow row;
        row.id = engine::QueueEntryId{static_cast<std::uint64_t>(i + 1)};
        row.media = {"media-" + std::to_string(i)};
        row.metadata.title = i == 2 ? "Latest" : "Obsolete";
        rows.push_back(row);
    }
    return std::make_shared<const gui::TrackProjectionInput>(std::move(rows));
}

TEST(TrackProjection, LatestQueryReplacesBlockedWorkAndOwnsItsSearchFacts)
{
    QObject owner;
    Gate gate;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    std::vector<gui::TrackProjectionResult> results;
    auto projection = gui::make_track_projection(
        owner,
        [&](auto result) {
            results.push_back(std::move(result));
            loop.quit();
        },
        [&](auto generation, auto stop) {
            if (generation == 1)
                gate.enter(stop);
        }
    );
    auto source = input();
    gui::TrackViewState old;
    old.query.title = "Obsolete";
    const auto first = projection->request(source, old);
    ASSERT_TRUE(gate.wait());
    gui::TrackViewState current;
    current.query.title = "Latest";
    const auto last = projection->request(source, current);
    source.reset();
    timeout.start(10000);
    loop.exec();
    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results[0].generation, last);
    EXPECT_NE(last, first);
    ASSERT_EQ(results[0].rows.size(), 1);
    EXPECT_EQ(results[0].rows[0].metadata.title, "Latest");
}

TEST(TrackProjection, ShutdownCancelsBlockedWorkAndNoPostedResultCanRetainTheReceiver)
{
    QObject owner;
    Gate gate;
    int deliveries = 0;
    auto projection = gui::make_track_projection(
        owner, [&](auto) { ++deliveries; }, [&](auto, auto stop) { gate.enter(stop); }
    );
    auto source = input();
    std::weak_ptr<const gui::TrackProjectionInput> released = source;
    static_cast<void>(projection->request(source, {}));
    source.reset();
    ASSERT_TRUE(gate.wait());
    projection.reset();
    QCoreApplication::processEvents();
    EXPECT_EQ(deliveries, 0);
    EXPECT_TRUE(released.expired());
}

TEST(TrackProjection, RetainsTheMutableObserverIdentityAcrossDeliveredRequests)
{
    QObject owner;
    std::vector<int> calls;
    auto projection = gui::make_track_projection(owner,
        [&, count = 0](auto) mutable { calls.push_back(++count); });
    for (int expected = 1; expected <= 2; ++expected) {
        static_cast<void>(projection->request(input(), {}));
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
        while (calls.size() < static_cast<std::size_t>(expected) &&
            std::chrono::steady_clock::now() < deadline)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        ASSERT_EQ(calls.size(), static_cast<std::size_t>(expected));
    }
    EXPECT_EQ(calls, (std::vector{1, 2}));
}

TEST(TrackProjection, FactoryRejectsAnOwnerFromAnotherThreadBeforeCreatingTheReceiver)
{
    QObject owner;
    bool rejected{};
    std::thread other{[&] {
        try {
            static_cast<void>(gui::make_track_projection(owner, [](auto) { }));
        } catch (const std::logic_error&) {
            rejected = true;
        }
    }};
    other.join();
    EXPECT_TRUE(rejected);
}

TEST(TrackProjection, AClosedQtContextCannotReceiveAResultFromTheRemainingWorker)
{
    auto owner = std::make_unique<QObject>();
    Gate gate;
    int calls{};
    auto projection = gui::make_track_projection(
        *owner, [&](auto) { ++calls; }, [&](auto, auto stop) { gate.enter(stop); }
    );
    auto source = input();
    const std::weak_ptr<const gui::TrackProjectionInput> released = source;
    static_cast<void>(projection->request(source, {}));
    source.reset();
    ASSERT_TRUE(gate.wait());
    owner.reset();
    projection->stop();
    QCoreApplication::processEvents();
    EXPECT_EQ(calls, 0);
    EXPECT_TRUE(released.expired());
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app{argc, argv};
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
