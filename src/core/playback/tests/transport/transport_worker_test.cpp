/**
 * @file
 * @brief Tests for nonblocking transport commands and worker startup failure.
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

#include <transport/transport_worker.hpp>

#include <fakes/decoder.hpp>
#include <fakes/output.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <functional>
#include <memory>
#include <span>
#include <stdexcept>
#include <thread>

namespace {

using wiola::engine::TransportWorker;
using wiola::pcm::Frames;
using wiola::testing::FakeDecoder;
using wiola::testing::FakeOutput;
using wiola::testing::Gate;
using State = wiola::engine::PlaybackState;

// Fail once at a controlled worker phase; all observations below happen after joining it.
class ThrowingDecoder final : public wiola::media::AudioDecoder {
public:
    enum class Phase {
        priming,
        decoding,
        seeking,
    };

    ThrowingDecoder(Phase phase, const FakeOutput& output, Gate* reading = nullptr)
        : AudioDecoder{{}, Frames{4096}}
        , phase_{phase}
        , output_{output}
        , reading_{reading}
    {
    }

    [[nodiscard]] bool failed() const noexcept { return failed_; }

private:
    wiola::media::Result<wiola::media::DecodeOutcome> decode(std::span<float> output,
        Frames frames) override
    {
        if (output_.running() && !held_) {
            held_ = true;
            if (reading_)
                reading_->wait();
        }
        if (!failed_ &&
            (phase_ == Phase::priming || (phase_ == Phase::decoding && output_.running())))
            fail();
        std::fill(output.begin(), output.end(), 0.0F);
        return wiola::media::DecodeOutcome{.frames = frames};
    }

    wiola::media::Result<wiola::media::SeekOutcome> seek_frame(Frames target) override
    {
        if (!failed_ && phase_ == Phase::seeking)
            fail();
        return wiola::media::SeekOutcome{.target = target, .achieved = target};
    }

    [[noreturn]] void fail()
    {
        failed_ = true;
        throw std::runtime_error{"decoder failed"};
    }

    Phase phase_;
    const FakeOutput& output_;
    Gate* reading_;
    bool held_{false};
    bool failed_{false};
};

TEST(TransportWorker, PrimingExceptionFaultsWithoutStartingOutput)
{
    FakeOutput output;
    auto source{std::make_unique<ThrowingDecoder>(ThrowingDecoder::Phase::priming, output)};
    wiola::pcm::PcmQueue buffer{source->spec(), Frames{512}};
    TransportWorker worker{std::move(source), buffer.producer(), output};

    ASSERT_TRUE(worker.start());
    worker.wait();

    EXPECT_EQ(worker.state(), State::faulted);
    EXPECT_EQ(output.num_starts(), 0);
    EXPECT_FALSE(output.running());
}

TEST(TransportWorker, DecodeExceptionStopsOutputAndAllowsAnotherStart)
{
    FakeOutput output;
    auto source{std::make_unique<ThrowingDecoder>(ThrowingDecoder::Phase::decoding, output)};
    wiola::pcm::PcmQueue buffer{source->spec(), Frames{512}};
    TransportWorker worker{std::move(source), buffer.producer(), output};

    ASSERT_TRUE(worker.start());
    worker.wait();

    EXPECT_EQ(worker.state(), State::faulted);
    EXPECT_EQ(output.num_starts(), 1);
    EXPECT_EQ(output.num_stops(), 1);
    EXPECT_FALSE(output.running());

    EXPECT_TRUE(worker.start());
    worker.stop();
    worker.wait();
    EXPECT_EQ(worker.state(), State::stopped);
    EXPECT_FALSE(output.running());
}

TEST(TransportWorker, AcceptedStopWinsOverALaterDecoderException)
{
    FakeOutput output;
    Gate reading;
    auto source{
        std::make_unique<ThrowingDecoder>(ThrowingDecoder::Phase::decoding, output, &reading)};
    wiola::pcm::PcmQueue buffer{source->spec(), Frames{512}};
    TransportWorker worker{std::move(source), buffer.producer(), output};

    ASSERT_TRUE(worker.start());
    reading.wait_until_entered();
    worker.stop();
    reading.release();
    worker.wait();

    EXPECT_EQ(worker.state(), State::stopped);
    EXPECT_FALSE(output.running());
}

TEST(TransportWorker, SeekExceptionAfterOutputStartedLeavesItStopped)
{
    FakeOutput output;
    Gate reading;
    auto source{
        std::make_unique<ThrowingDecoder>(ThrowingDecoder::Phase::seeking, output, &reading)};
    const auto* decoder{source.get()};
    wiola::pcm::PcmQueue buffer{source->spec(), Frames{512}};
    TransportWorker worker{std::move(source), buffer.producer(), output};

    ASSERT_TRUE(worker.start());
    reading.wait_until_entered();
    worker.seek(Frames{8});
    reading.release();
    worker.wait();

    EXPECT_TRUE(decoder->failed());
    EXPECT_EQ(worker.state(), State::faulted);
    EXPECT_EQ(output.num_starts(), 1);
    EXPECT_FALSE(output.running());
}

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

TEST(TransportWorker, StopDuringInitialSeekNeverStartsOutputOrPrimes)
{
    auto source{std::make_unique<FakeDecoder>()};
    auto* decoder{source.get()};
    Gate seeking;
    source->block_seeks_on(seeking);
    wiola::pcm::PcmQueue buffer{source->spec(), Frames{512}};
    FakeOutput output;
    TransportWorker worker{std::move(source), buffer.producer(), output};
    worker.seek(Frames{8});
    ASSERT_TRUE(worker.start());
    seeking.wait_until_entered();
    worker.stop();
    seeking.release();
    worker.wait();

    EXPECT_EQ(worker.state(), State::stopped);
    EXPECT_EQ(output.num_starts(), 0);
    EXPECT_EQ(decoder->num_reads(), 0);
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
    wiola::engine::TransportConfig config;
    config.poll_interval = std::chrono::milliseconds{5000};
    TransportWorker worker{std::move(source), buffer.producer(), output, {}, config};

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

TEST(TransportWorker, UsesItsOwnDecodeChunkConfiguration)
{
    for (const std::size_t chunk_frames : {1U, 4U}) {
        auto source{std::make_unique<FakeDecoder>()};
        FakeDecoder* played{source.get()};
        wiola::pcm::PcmQueue buffer{source->spec(), Frames{512}};
        FakeOutput output;
        wiola::engine::TransportConfig config;
        config.decode_chunk = Frames{chunk_frames};
        TransportWorker worker{std::move(source), buffer.producer(), output, {}, config};

        ASSERT_TRUE(worker.start());
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
        while (played->num_reads() == 0) {
            ASSERT_LT(std::chrono::steady_clock::now(), deadline);
            std::this_thread::yield();
        }

        worker.stop();
        worker.wait();
        EXPECT_EQ(played->last_render_samples(), chunk_frames * 2);
    }
}

TEST(TransportWorker, RejectsAnInvalidConfigurationBeforeStarting)
{
    auto source{std::make_unique<FakeDecoder>()};
    wiola::pcm::PcmQueue buffer{source->spec(), Frames{512}};
    FakeOutput output;
    wiola::engine::TransportConfig config;
    config.decode_chunk = Frames{};

    EXPECT_THROW((TransportWorker{std::move(source), buffer.producer(), output, {}, config}),
        std::invalid_argument);
    EXPECT_EQ(output.num_starts(), 0);
}

} // namespace
