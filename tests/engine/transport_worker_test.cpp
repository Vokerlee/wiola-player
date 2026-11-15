/**
 * @file
 * @brief Tests for nonblocking transport commands and worker startup failure.
 * @author Roman Glaz
 * @copyright © 2026, <vokerlee@gmail.com>
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

#include <engine/transport/transport_worker.hpp>

#include <fakes/decoder.hpp>
#include <fakes/output.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <functional>
#include <memory>
#include <stdexcept>
#include <thread>

namespace {

using wiola::engine::TransportWorker;
using wiola::pcm::Frames;
using wiola::testing::FakeDecoder;
using wiola::testing::FakeOutput;
using wiola::testing::Gate;
using State = wiola::engine::Playback::State;

TEST(TransportWorker, StartAndCommandsDoNotWaitForABlockedDecoderRead)
{
    auto source{std::make_unique<FakeDecoder>()};
    Gate reading;
    source->block_reads_on(reading);
    wiola::pcm::PcmQueue buffer{source->spec(), Frames{512}};
    FakeOutput output;
    TransportWorker worker{std::move(source), buffer.producer(), output};

    ASSERT_TRUE(worker.start());
    reading.wait_until_entered();
    EXPECT_TRUE(worker.pause());
    EXPECT_EQ(worker.state(), State::paused);

    worker.stop();
    reading.release();
    worker.wait();

    EXPECT_EQ(worker.state(), State::stopped);
    EXPECT_FALSE(output.running());
}

TEST(TransportWorker, OutputStartFailureIsEventualRatherThanRequestRefusal)
{
    auto source{std::make_unique<FakeDecoder>()};
    wiola::pcm::PcmQueue buffer{source->spec(), Frames{512}};
    FakeOutput output;
    output.refuse_starts();
    TransportWorker worker{std::move(source), buffer.producer(), output};

    EXPECT_TRUE(worker.start());
    worker.wait();

    EXPECT_EQ(worker.state(), State::faulted);
    EXPECT_FALSE(output.running());
}

TEST(TransportWorker, ThreadLaunchFailureLeavesNoRunningOutput)
{
    auto source{std::make_unique<FakeDecoder>()};
    wiola::pcm::PcmQueue buffer{source->spec(), Frames{512}};
    FakeOutput output;
    const TransportWorker::ThreadFactory fail{
        [](std::function<void()>) -> std::jthread { throw std::runtime_error{"no thread"}; }};
    TransportWorker worker{std::move(source), buffer.producer(), output, fail};

    EXPECT_FALSE(worker.start());
    worker.wait();

    EXPECT_EQ(worker.state(), State::faulted);
    EXPECT_EQ(output.num_starts(), 0);
    EXPECT_FALSE(output.running());
}

TEST(TransportWait, NotificationBeforeWaitingIsNotLost)
{
    wiola::engine::TransportWait waiting{std::chrono::milliseconds{5000}};
    waiting.notify();

    const auto before = std::chrono::steady_clock::now();
    waiting.wait();

    EXPECT_LT(std::chrono::steady_clock::now() - before, std::chrono::milliseconds{100});
}

TEST(TransportWorker, StopInterruptsALongOutputProgressPoll)
{
    auto source{std::make_unique<FakeDecoder>()};
    wiola::pcm::PcmQueue buffer{source->spec(), Frames{512}};
    FakeOutput output;
    TransportWorker worker{std::move(source), buffer.producer(), output, {},
        std::chrono::milliseconds{5000}};

    ASSERT_TRUE(worker.start());
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    while (!output.running()) {
        ASSERT_LT(std::chrono::steady_clock::now(), deadline);
        std::this_thread::yield();
    }

    const auto before = std::chrono::steady_clock::now();
    worker.stop();
    worker.wait();

    EXPECT_LT(std::chrono::steady_clock::now() - before, std::chrono::seconds{1});
    EXPECT_EQ(worker.state(), State::stopped);
}

} // namespace
