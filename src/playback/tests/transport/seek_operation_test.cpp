/**
 * @file
 * @brief Tests for playing one decoder through a device.
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

#include <fakes/decoder.hpp>
#include <fakes/output.hpp>
#include <transport/seek_operation.hpp>

#include <gtest/gtest.h>

#include <array>
#include <span>
#include <thread>
#include <vector>

namespace {

using wiola::engine::Playback;
using wiola::engine::Playhead;
using wiola::engine::SeekOperation;
using wiola::pcm::Frames;
using wiola::testing::FakeDecoder;
using wiola::testing::FakeOutput;
using wiola::testing::Gate;
using Outcome = SeekOperation::Outcome;

struct SeekRig {
    explicit SeekRig(wiola::media::AudioDecoder& source)
        : queue{source.spec(), Frames{8}}
        , producer{queue.producer()}
        , consumer{queue.consumer()}
        , control{wiola::audio::Output::acquire(output)}
        , head{source.spec().num_channels}
        , applier{head.applier()}
        , block(source.spec().samples_per(Frames{4}))
        , seek{source, producer, control, output.get(), applier, started, block, playback}
    {
        static_cast<void>(playback.begin());
    }

    ~SeekRig() { control.stop(); }

    bool start_output() noexcept { return started = control.start(); }

    wiola::pcm::PcmQueue queue;
    wiola::pcm::PcmQueue::Producer producer;
    wiola::pcm::PcmQueue::Consumer consumer;
    wiola::core::Lease<FakeOutput> output = wiola::core::Lease<FakeOutput>::make();
    wiola::audio::Output::Control control;
    Playback playback;
    Playhead head;
    Playhead::Applier applier;
    bool started{false};
    std::vector<float> block;
    SeekOperation seek;
};

class UnreadableDecoder final : public wiola::media::AudioDecoder {
public:
    UnreadableDecoder()
        : AudioDecoder{{}, Frames{32}}
    {
    }

private:
    wiola::media::Result<wiola::media::DecodeOutcome> decode(std::span<float>, Frames) override
    {
        return wiola::media::unexpected{
            wiola::media::Error{.category = wiola::media::ErrorCategory::read_failed,
                                .detail = "read refused"}
        };
    }

    wiola::media::Result<wiola::media::SeekOutcome> seek_frame(Frames target) override
    {
        return wiola::media::SeekOutcome{.target = target, .achieved = target};
    }
};

TEST(SeekTransaction, InitialPrimingRetainsTheCursorWithoutSeeking)
{
    FakeDecoder source{Frames{32}};
    SeekRig rig{source};

    EXPECT_EQ(rig.seek.begin(false), Outcome::preserved);
    EXPECT_EQ(source.num_seeks(), 0);
    EXPECT_EQ(rig.applier.num_pushed(), rig.block.size() * 2);
    EXPECT_EQ(rig.head.position(Frames{}), Frames{});
}

TEST(SeekTransaction, ReplayRewindsAndDiscardsPreviouslyBufferedAudio)
{
    FakeDecoder source{Frames{32}};
    SeekRig rig{source};

    ASSERT_EQ(rig.seek.begin(false), Outcome::preserved);
    ASSERT_GT(source.position_frames(), Frames{});
    ASSERT_TRUE(rig.start_output());

    rig.output->play(Frames{3});
    EXPECT_EQ(rig.seek.begin(true), Outcome::applied);
    EXPECT_EQ(source.last_seek(), Frames{});
    EXPECT_FALSE(rig.output->running());
    EXPECT_FALSE(rig.started);
    EXPECT_EQ(rig.head.position(Frames{3}), Frames{});
    // The consumer owns the read cursor and applies the discard on its next callback.
    EXPECT_EQ(rig.applier.num_pushed(), 0U);

    std::array<float, 16> stale{};

    EXPECT_EQ(rig.consumer.pop(stale), 0U);
    EXPECT_EQ(source.position_frames(), Frames{});
}

TEST(SeekTransaction, NoPendingRequestLeavesOutputAndBufferedAudioAlone)
{
    FakeDecoder source{Frames{32}};
    SeekRig rig{source};

    ASSERT_TRUE(rig.start_output());

    const std::array samples{0.25F, -0.25F};

    ASSERT_EQ(rig.producer.push(samples), samples.size());
    EXPECT_EQ(rig.seek.apply(), Outcome::preserved);
    EXPECT_TRUE(rig.output->running());
    EXPECT_EQ(source.num_reads(), 0);
    EXPECT_EQ(source.num_seeks(), 0);

    std::array<float, 2> retained{};

    EXPECT_EQ(rig.consumer.pop(retained), retained.size());
    EXPECT_EQ(retained, samples);
}

TEST(SeekTransaction, FailedSeekCommitsThePreservedCursor)
{
    FakeDecoder source{Frames{32}};

    ASSERT_TRUE(source.seek(Frames{6}));

    source.refuse_seeks();

    SeekRig rig{source};
    rig.head.request_seek(Frames{12});
    EXPECT_EQ(rig.seek.apply(), Outcome::preserved);
    EXPECT_EQ(rig.head.position(Frames{}), Frames{6});
    EXPECT_FALSE(rig.head.seek_outstanding());
}

TEST(SeekTransaction, ExactLengthValidationBelongsToTheDecoder)
{
    FakeDecoder source{Frames{32}};
    SeekRig rig{source};
    rig.head.request_seek(Frames{33});
    EXPECT_EQ(rig.seek.apply(), Outcome::preserved);
    EXPECT_EQ(source.num_seeks(), 0);
    EXPECT_EQ(rig.head.position(Frames{}), Frames{});
}

TEST(SeekTransaction, ApproximateSeekCommitsItsAchievedPosition)
{
    FakeDecoder source{Frames{32}};
    source.achieve_next_seek_at(Frames{10});

    SeekRig rig{source};
    rig.head.request_seek(Frames{12});
    EXPECT_EQ(rig.seek.apply(), Outcome::applied);
    EXPECT_EQ(rig.head.position(Frames{}), Frames{10});
    EXPECT_FALSE(rig.head.seek_outstanding());
}

TEST(SeekTransaction, ReadFailureIsReturnedWithTheCommittedPosition)
{
    UnreadableDecoder source;
    SeekRig rig{source};
    rig.head.request_seek(Frames{12});
    EXPECT_EQ(rig.seek.apply(), Outcome::read_failed);
    EXPECT_EQ(rig.head.position(Frames{}), Frames{12});
    EXPECT_EQ(rig.applier.num_pushed(), 0U);
}

TEST(SeekTransaction, EmptySourcePrimesSuccessfully)
{
    FakeDecoder source{Frames{0}};
    SeekRig rig{source};

    EXPECT_EQ(rig.seek.begin(false), Outcome::preserved);
    EXPECT_EQ(source.num_reads(), 0);
    EXPECT_EQ(rig.applier.num_pushed(), 0U);
}

TEST(SeekTransaction, StopBeforeExecutionPreservesThePendingRequestWithoutIO)
{
    FakeDecoder source{Frames{32}};
    SeekRig rig{source};
    rig.head.request_seek(Frames{12});
    rig.playback.finish(Playback::TerminalReason::stopped);
    EXPECT_EQ(rig.seek.apply(), Outcome::cancelled);
    EXPECT_EQ(source.num_seeks(), 0);
    EXPECT_EQ(source.num_reads(), 0);
    EXPECT_TRUE(rig.head.seek_outstanding());
}

TEST(SeekTransaction, StopDuringSeekAcknowledgesTheActualCursorWithoutPriming)
{
    FakeDecoder source{Frames{32}};
    Gate seeking;
    source.block_seeks_on(seeking);

    SeekRig rig{source};

    ASSERT_TRUE(rig.start_output());

    rig.head.request_seek(Frames{12});

    Outcome result{};
    std::jthread worker{[&] { result = rig.seek.apply(); }};

    seeking.wait_until_entered();
    rig.playback.finish(Playback::TerminalReason::stopped);
    seeking.release();
    worker.join();
    EXPECT_EQ(result, Outcome::cancelled);
    EXPECT_EQ(source.num_reads(), 0);
    EXPECT_FALSE(rig.output->running());
    EXPECT_EQ(rig.head.position(Frames{}), Frames{12});
}

TEST(SeekTransaction, NewerRequestDuringSeekSurvivesAcknowledgmentAndSkipsStalePriming)
{
    FakeDecoder source{Frames{32}};
    Gate seeking;
    source.block_seeks_on(seeking);

    SeekRig rig{source};
    rig.head.request_seek(Frames{8});

    Outcome result{};
    std::jthread worker{[&] { result = rig.seek.apply(); }};

    seeking.wait_until_entered();
    rig.head.request_seek(Frames{12});
    seeking.release();
    worker.join();
    EXPECT_EQ(result, Outcome::superseded);
    EXPECT_EQ(source.num_reads(), 0);
    EXPECT_TRUE(rig.head.seek_outstanding());
    EXPECT_EQ(rig.head.position(Frames{}), Frames{12});
    EXPECT_EQ(rig.seek.apply(), Outcome::applied);
    EXPECT_EQ(source.last_seek(), Frames{12});
    EXPECT_FALSE(rig.head.seek_outstanding());
}

TEST(SeekTransaction, StopDuringReadDoesNotSubmitTheCompletedStaleBlock)
{
    FakeDecoder source{Frames{32}};
    Gate reading;
    source.block_reads_on(reading);

    SeekRig rig{source};
    rig.head.request_seek(Frames{8});

    Outcome result{};
    std::jthread worker{[&] { result = rig.seek.apply(); }};

    reading.wait_until_entered();
    rig.playback.finish(Playback::TerminalReason::stopped);
    reading.release();
    worker.join();
    EXPECT_EQ(result, Outcome::cancelled);
    EXPECT_EQ(rig.applier.num_pushed(), 0U);
    EXPECT_EQ(rig.queue.size_approx(), 0U);
}

TEST(SeekTransaction, NewerRequestDuringReadPreventsStaleSubmission)
{
    FakeDecoder source{Frames{32}};
    Gate reading;
    source.block_reads_on(reading);

    SeekRig rig{source};
    rig.head.request_seek(Frames{8});

    Outcome result{};
    std::jthread worker{[&] { result = rig.seek.apply(); }};

    reading.wait_until_entered();
    rig.head.request_seek(Frames{12});
    reading.release();
    worker.join();
    EXPECT_EQ(result, Outcome::superseded);
    EXPECT_EQ(rig.applier.num_pushed(), 0U);
    EXPECT_EQ(rig.queue.size_approx(), 0U);
    EXPECT_EQ(rig.head.position(Frames{}), Frames{12});
}

} // namespace
