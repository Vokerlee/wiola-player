/**
 * @file
 * @brief Synchronized payload delivery to the desktop event loop.
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
#include <chrono>
#include <desktop/queued_delivery.hpp>
#include <gtest/gtest.h>
#include <latch>
#include <memory>
#include <thread>

namespace {

TEST(QueuedDelivery, TransfersWorkerPayloadsAndAllowsReentrantPosting)
{
    wiola::app::QueuedDelivery delivery{*QCoreApplication::instance()};
    const auto owner = std::this_thread::get_id();
    std::latch release{1};
    int received = 0;
    std::jthread worker{[&] {
        const std::string payload(1024, 'x');
        EXPECT_TRUE(delivery.post([&, payload] {
            EXPECT_EQ(std::this_thread::get_id(), owner);
            EXPECT_EQ(payload, std::string(1024, 'x'));
            ++received;
            EXPECT_TRUE(delivery.post([&] { ++received; }));
        }));
        release.wait();
    }};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    while (received != 2 && std::chrono::steady_clock::now() < deadline)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    EXPECT_EQ(received, 2);
    release.count_down();
}

TEST(QueuedDelivery, DestroyedContextDiscardsPendingAndFutureDeliveries)
{
    auto context = std::make_unique<QObject>();
    wiola::app::QueuedDelivery delivery{*context};
    int calls = 0;
    EXPECT_TRUE(delivery.post([&] { ++calls; }));
    context.reset();
    EXPECT_FALSE(delivery.post([&] { ++calls; }));
    QCoreApplication::sendPostedEvents(&delivery);
    EXPECT_EQ(calls, 0);
}

TEST(QueuedDelivery, ReceiverDestructionDiscardsPostedDeliveries)
{
    int calls = 0;
    {
        wiola::app::QueuedDelivery delivery{*QCoreApplication::instance()};
        EXPECT_FALSE(delivery.post({}));
        EXPECT_TRUE(delivery.post([&] { ++calls; }));
    }
    QCoreApplication::sendPostedEvents();
    EXPECT_EQ(calls, 0);
}

} // namespace

int main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    QCoreApplication application{argc, argv};
    return RUN_ALL_TESTS();
}
