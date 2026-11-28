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

#include "rig.hpp"

#include <buffering/buffer_source.hpp>
#include <buffering/pcm_queue.hpp>
#include <fakes/decoder.hpp>
#include <fakes/output.hpp>
#include <transport/player.hpp>
#include <transport/seek_operation.hpp>
#include <wiola/audio/output.hpp>
#include <wiola/media/audio_decoder.hpp>
#include <wiola/media/stream_spec.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <mutex>
#include <numbers>
#include <ranges>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace std::chrono_literals;
using namespace wiola::units::literals;
using wiola::engine::Player;
using wiola::pcm::StreamSpec;
using wiola::testing::Rig;
using State = wiola::engine::PlaybackState;
namespace units = wiola::units;

// Longer than the rig's priming buffer, with no filesystem or codec involved.
std::unique_ptr<wiola::testing::FakeDecoder> pcm_source()
{
    constexpr StreamSpec spec{.sample_rate = 44100_Hz, .num_channels = 2};
    constexpr units::Time duration{1.5};

    return std::make_unique<wiola::testing::FakeDecoder>(*spec.checked_frames_at(duration), spec);
}

/// Waits for `predicate`, so a test never depends on how fast a device happens to run.
template<typename Predicate>
bool eventually(Predicate predicate, std::chrono::milliseconds limit = 5s)
{
    const auto deadline = std::chrono::steady_clock::now() + limit;

    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate())
            return true;

        std::this_thread::sleep_for(10ms);
    }

    return predicate();
}

class FinalBlockSource final : public wiola::media::AudioDecoder {
public:
    FinalBlockSource()
        : wiola::media::AudioDecoder{
              StreamSpec{units::Frequency{44100.0}, 2},
              wiola::pcm::Frames{8}
    }
    {
    }

    [[nodiscard]] int num_reads() const noexcept { return num_reads_.load(); }

protected:
    wiola::media::Result<wiola::media::DecodeOutcome> decode(std::span<float> output,
        wiola::pcm::Frames num_frames) override
    {
        ++num_reads_;
        std::fill_n(output.begin(), num_frames.count() * 2, 0.25F);

        return wiola::media::DecodeOutcome{.frames = num_frames};
    }

    wiola::media::Result<wiola::media::SeekOutcome>
    seek_frame(wiola::pcm::Frames frame_index) override
    {
        return wiola::media::SeekOutcome{.target = frame_index, .achieved = frame_index};
    }

private:
    std::atomic<int> num_reads_{0};
};

class UnknownLengthSource final : public wiola::media::AudioDecoder {
public:
    UnknownLengthSource()
        : wiola::media::AudioDecoder{
              wiola::media::DecoderStreamInfo{.spec = StreamSpec{units::Frequency{44100.0}, 2},
                                              .length = std::nullopt,
                                              .length_accuracy = wiola::media::LengthAccuracy::unknown}
    }
    {
    }

protected:
    wiola::media::Result<wiola::media::DecodeOutcome> decode(std::span<float> output,
        wiola::pcm::Frames num_frames) override
    {
        constexpr wiola::pcm::Frames length{8};
        const wiola::pcm::Frames remaining{length - position_frames()};
        const wiola::pcm::Frames produced{std::min(num_frames, remaining)};
        std::fill_n(output.begin(), produced.count() * 2, 0.0F);

        return wiola::media::DecodeOutcome{.frames = produced,
            .end_of_stream = position_frames() + produced == length};
    }

    wiola::media::Result<wiola::media::SeekOutcome>
    seek_frame(wiola::pcm::Frames frame_index) override
    {
        if (frame_index > wiola::pcm::Frames{8})
            return wiola::media::unexpected{
                wiola::media::Error{.category = wiola::media::ErrorCategory::seek_failed,
                                    .detail = "past actual end"}
            };

        return wiola::media::SeekOutcome{.target = frame_index, .achieved = frame_index};
    }
};

TEST(SeekOperation, AcceptsUnknownLengthWithoutTreatingItAsAnExactZero)
{
    UnknownLengthSource source;
    wiola::pcm::PcmQueue queue{source.spec(), wiola::pcm::Frames{4}};
    auto producer{queue.producer()};
    wiola::core::Lease<wiola::testing::FakeOutput> output =
        wiola::core::Lease<wiola::testing::FakeOutput>::make();
    wiola::engine::Playhead playhead{source.spec().num_channels};
    auto applier{playhead.applier()};
    wiola::engine::Playback playback;
    std::vector<float> block(source.spec().samples_per(wiola::pcm::Frames{4}));

    playhead.request_seek(wiola::pcm::Frames{4});

    const auto plan = wiola::engine::SeekPlan::pending(applier.claim(), playback.finished());

    EXPECT_EQ(wiola::engine::execute_seek(plan, source, producer, output.get(), applier, block,
                  playback),
        wiola::engine::SeekOutcome::applied);
    EXPECT_FALSE(playhead.seek_outstanding());
    EXPECT_EQ(playhead.position(wiola::pcm::Frames{}), wiola::pcm::Frames{4});
}

class FailFirstReadSource final : public wiola::media::AudioDecoder {
public:
    FailFirstReadSource()
        : wiola::media::AudioDecoder{
              StreamSpec{units::Frequency{44100.0}, 2},
              wiola::pcm::Frames{8}
    }
    {
    }

protected:
    wiola::media::Result<wiola::media::DecodeOutcome> decode(std::span<float> output,
        wiola::pcm::Frames num_frames) override
    {
        if (num_reads_++ == 0)
            return wiola::media::unexpected{
                wiola::media::Error{.category = wiola::media::ErrorCategory::read_failed,
                                    .detail = "priming failed"}
            };

        std::fill_n(output.begin(), num_frames.count() * 2, 0.0F);

        return wiola::media::DecodeOutcome{.frames = num_frames};
    }

    wiola::media::Result<wiola::media::SeekOutcome>
    seek_frame(wiola::pcm::Frames frame_index) override
    {
        return wiola::media::SeekOutcome{.target = frame_index, .achieved = frame_index};
    }

private:
    int num_reads_{0};
};

} // namespace

TEST(Player, PlaysControlledPcmToTheEndOnItsOwn)
{
    auto source = pcm_source();

    ASSERT_NE(source, nullptr);

    const wiola::media::AudioDecoder* played{source.get()};
    Rig rig{std::move(source)};
    Player& player{rig.player};

    ASSERT_TRUE(player.start());

    EXPECT_TRUE(player.playing());

    rig.play_out();
    player.wait();

    EXPECT_TRUE(player.finished());
    EXPECT_TRUE(played->exhausted());
}

/// The point of the thread: the caller is free while the audio is still going.
TEST(Player, DrainsFramesReturnedTogetherWithEndOfStream)
{
    auto source{std::make_unique<FinalBlockSource>()};
    const FinalBlockSource* decoded{source.get()};
    const StreamSpec spec{source->spec()};
    wiola::pcm::PcmQueue buffer{spec, wiola::pcm::Frames{4}};
    auto consumer{buffer.consumer()};
    wiola::core::Lease<wiola::testing::FakeOutput> output =
        wiola::core::Lease<wiola::testing::FakeOutput>::make();
    wiola::engine::TransportConfig config;
    config.decode_chunk = wiola::pcm::Frames{4};

    Player player{std::move(source), buffer.producer(), output, config};
    std::array<float, 8> played{};

    ASSERT_TRUE(player.start());
    ASSERT_TRUE(eventually([decoded] { return decoded->num_reads() == 2; }));
    ASSERT_EQ(consumer.pop(played), played.size());

    output->play(wiola::pcm::Frames{4});
    ASSERT_TRUE(eventually([&buffer] { return buffer.size_approx() == 8; }));
    EXPECT_FALSE(player.finished());

    ASSERT_EQ(consumer.pop(played), played.size());

    output->play(wiola::pcm::Frames{4});
    player.wait();
    EXPECT_EQ(player.state(), State::ended);
    EXPECT_EQ(output->frames_played(), wiola::pcm::Frames{8});
    EXPECT_EQ(player.time_played(), units::Time{8.0 / 44100.0});
}

TEST(Player, PrimingReadFailureFaultsInsteadOfBeingRetriedAsNormalPlayback)
{
    auto source{std::make_unique<FailFirstReadSource>()};
    const StreamSpec spec{source->spec()};
    wiola::pcm::PcmQueue buffer{spec, wiola::pcm::Frames{8}};
    wiola::core::Lease<wiola::testing::FakeOutput> output =
        wiola::core::Lease<wiola::testing::FakeOutput>::make();
    wiola::engine::TransportConfig config;
    config.decode_chunk = wiola::pcm::Frames{8};

    Player player{std::move(source), buffer.producer(), output, config};

    ASSERT_TRUE(player.start());

    player.wait();

    EXPECT_EQ(player.state(), State::faulted);
    EXPECT_EQ(output->num_starts(), 0);
}

TEST(Player, UnknownDurationPlaysToActualExhaustion)
{
    Rig rig{std::make_unique<UnknownLengthSource>()};
    Player& player{rig.player};

    EXPECT_EQ(player.total_time(), std::nullopt);
    EXPECT_TRUE(player.seekable());
    ASSERT_TRUE(player.start());

    rig.play_out();
    player.wait();

    EXPECT_EQ(player.state(), State::ended);
    EXPECT_EQ(player.total_time(), std::nullopt);
    EXPECT_EQ(player.time_played(), units::Time{8.0 / 44100.0});
}

TEST(Player, UnknownDurationKeepsTransportAndAbsoluteSeekAvailable)
{
    auto source{std::make_unique<UnknownLengthSource>()};
    const StreamSpec spec{source->spec()};
    wiola::pcm::PcmQueue buffer{spec, wiola::pcm::Frames{16}};
    wiola::core::Lease<wiola::testing::FakeOutput> output =
        wiola::core::Lease<wiola::testing::FakeOutput>::make();
    Player player{std::move(source), buffer.producer(), output};

    ASSERT_TRUE(player.start());
    ASSERT_TRUE(player.pause());

    player.seek(units::Time{2.0 / 44100.0});
    EXPECT_EQ(player.time_played(), units::Time{2.0 / 44100.0});
    ASSERT_TRUE(player.resume());

    player.stop();
    player.wait();

    EXPECT_EQ(player.state(), State::stopped);
    EXPECT_EQ(player.total_time(), std::nullopt);
}

TEST(Player, StartReturnsBeforePlaybackEnds)
{
    auto source = pcm_source();

    ASSERT_NE(source, nullptr);

    Rig rig{std::move(source)};
    Player& player{rig.player};

    ASSERT_TRUE(player.start());

    EXPECT_FALSE(player.finished());
}

TEST(Player, StopEndsPlaybackWithoutDrainingTheSource)
{
    auto source = pcm_source();
    const auto* decoder{source.get()};
    Rig rig{std::move(source)};
    Player& player{rig.player};

    ASSERT_TRUE(player.start());

    player.stop();
    player.wait();

    EXPECT_EQ(player.state(), State::stopped);
    EXPECT_TRUE(player.finished());
    EXPECT_FALSE(player.playing());
    EXPECT_FALSE(rig.output->running());
    EXPECT_FALSE(decoder->exhausted());
}

TEST(Player, StopIsHarmlessAfterPlaybackHasEnded)
{
    auto source = pcm_source();

    ASSERT_NE(source, nullptr);

    Rig rig{std::move(source)};
    Player& player{rig.player};

    ASSERT_TRUE(player.start());

    rig.play_out();
    player.wait();
    player.stop();
    player.stop();

    EXPECT_TRUE(player.finished());
}

TEST(Player, PauseSilencesAndResumeContinues)
{
    auto source = pcm_source();

    ASSERT_NE(source, nullptr);

    Rig rig{std::move(source)};
    Player& player{rig.player};

    ASSERT_TRUE(player.start());

    ASSERT_TRUE(player.pause());
    EXPECT_FALSE(player.playing());

    // Pausing is not ending: the player is still there to be resumed.
    EXPECT_FALSE(player.finished());

    // There is nothing left to silence, so the second one has nothing to report.
    EXPECT_FALSE(player.pause());
    EXPECT_FALSE(player.playing());

    ASSERT_TRUE(player.resume());
    EXPECT_TRUE(player.playing());

    rig.play_out();
    EXPECT_TRUE(eventually([&player] { return player.finished(); }));
}

TEST(Player, SeekMovesTheSource)
{
    auto source = pcm_source();
    const auto* decoder{source.get()};
    const auto target{*source->spec().checked_frames_at(1300_ms)};
    const auto remaining{source->num_frames() - target};
    Rig rig{std::move(source)};
    Player& player{rig.player};

    ASSERT_TRUE(player.start());
    ASSERT_TRUE(player.pause());

    player.seek(1300_ms);
    EXPECT_FALSE(player.playing());

    ASSERT_TRUE(player.resume());
    ASSERT_TRUE(eventually([&] {
        return decoder->last_seek() == target && rig.output->running();
    }));

    rig.play_out();
    player.wait();

    EXPECT_EQ(decoder->last_seek(), target);
    EXPECT_EQ(rig.output->frames_played(), remaining);
    EXPECT_EQ(player.state(), State::ended);
}

TEST(Player, SeekBeyondTheEndIsIgnored)
{
    auto source{std::make_unique<wiola::testing::FakeDecoder>(wiola::pcm::Frames{100})};
    wiola::testing::FakeDecoder* moved{source.get()};
    wiola::pcm::PcmQueue buffer{StreamSpec{}, wiola::pcm::Frames{512}};
    wiola::core::Lease<wiola::testing::FakeOutput> output =
        wiola::core::Lease<wiola::testing::FakeOutput>::make();
    Player player{std::move(source), buffer.producer(), output};

    player.seek(3600_s);

    EXPECT_EQ(player.time_played(), units::Time{});

    ASSERT_TRUE(player.start());
    EXPECT_EQ(moved->num_seeks(), 0);
    EXPECT_FALSE(player.finished());

    player.stop();
    player.wait();
}

TEST(Player, IgnoresNonFiniteAndUnrepresentableSeekTimes)
{
    auto source{std::make_unique<wiola::testing::FakeDecoder>(wiola::pcm::Frames{100})};
    wiola::testing::FakeDecoder* moved{source.get()};
    wiola::pcm::PcmQueue buffer{StreamSpec{}, wiola::pcm::Frames{512}};
    wiola::core::Lease<wiola::testing::FakeOutput> output =
        wiola::core::Lease<wiola::testing::FakeOutput>::make();
    Player player{std::move(source), buffer.producer(), output};
    const std::array positions{units::Time{std::numeric_limits<double>::quiet_NaN()},
        units::Time{std::numeric_limits<double>::infinity()},
        units::Time{-std::numeric_limits<double>::infinity()},
        units::Time{std::numeric_limits<double>::max()}};

    for (const units::Time position : positions) {
        player.seek(position);
        EXPECT_EQ(player.time_played(), units::Time{});
    }

    ASSERT_TRUE(player.start());
    EXPECT_EQ(moved->num_seeks(), 0);

    player.stop();
    player.wait();
}

TEST(Player, UnseekableMediaDoesNotPublishOrExecuteASeek)
{
    auto source{
        std::make_unique<wiola::testing::FakeDecoder>(wiola::media::DecoderStreamInfo{.spec = {},
            .length = wiola::pcm::Frames{4096},
            .length_accuracy = wiola::media::LengthAccuracy::exact,
            .seekable = false})};
    const auto* decoder{source.get()};
    wiola::pcm::PcmQueue buffer{source->spec(), wiola::pcm::Frames{512}};
    wiola::core::Lease<wiola::testing::FakeOutput> output =
        wiola::core::Lease<wiola::testing::FakeOutput>::make();
    Player player{std::move(source), buffer.producer(), output};

    EXPECT_FALSE(player.seekable());

    player.seek(0.01_s);

    EXPECT_EQ(player.time_played(), units::Time{});
    EXPECT_EQ(player.state(), State::idle);
    EXPECT_EQ(decoder->num_seeks(), 0);
    EXPECT_EQ(decoder->num_reads(), 0);
}

TEST(Player, ClampsANegativeSeekToTheBeginning)
{
    auto source{std::make_unique<wiola::testing::FakeDecoder>(wiola::pcm::Frames{100})};
    wiola::testing::FakeDecoder* moved{source.get()};
    wiola::pcm::PcmQueue buffer{StreamSpec{}, wiola::pcm::Frames{512}};
    wiola::core::Lease<wiola::testing::FakeOutput> output =
        wiola::core::Lease<wiola::testing::FakeOutput>::make();
    Player player{std::move(source), buffer.producer(), output};

    player.seek(-1_s);

    ASSERT_TRUE(player.start());
    ASSERT_TRUE(eventually([moved] { return moved->num_seeks() == 1; }));
    EXPECT_EQ(moved->last_seek(), wiola::pcm::Frames{});

    player.stop();
    player.wait();
}

TEST(Player, FailedSeekBeforeStartKeepsThePreservedBeginning)
{
    const StreamSpec spec{units::Frequency{100.0}, 2};
    auto source{std::make_unique<wiola::testing::FakeDecoder>(wiola::pcm::Frames{10000}, spec)};
    wiola::testing::FakeDecoder* moved{source.get()};
    moved->refuse_seeks();

    wiola::pcm::PcmQueue buffer{StreamSpec{}, wiola::pcm::Frames{512}};
    wiola::core::Lease<wiola::testing::FakeOutput> output =
        wiola::core::Lease<wiola::testing::FakeOutput>::make();
    Player player{std::move(source), buffer.producer(), output};

    player.seek(1_s);
    ASSERT_TRUE(player.start());

    ASSERT_TRUE(eventually([moved, &player] {
        return moved->num_seeks() == 1 && player.time_played() == units::Time{};
    }));
    EXPECT_EQ(player.state(), State::playing);

    player.stop();
    player.wait();
}

TEST(Player, FailedSeekDoesNotPublishTheRequestedPosition)
{
    const StreamSpec spec{units::Frequency{100.0}, 2};
    auto source{std::make_unique<wiola::testing::FakeDecoder>(wiola::pcm::Frames{10000}, spec)};
    wiola::testing::FakeDecoder* moved{source.get()};
    moved->refuse_seeks();

    wiola::pcm::PcmQueue buffer{StreamSpec{}, wiola::pcm::Frames{512}};
    wiola::core::Lease<wiola::testing::FakeOutput> output =
        wiola::core::Lease<wiola::testing::FakeOutput>::make();
    Player player{std::move(source), buffer.producer(), output};

    ASSERT_TRUE(player.start());
    ASSERT_TRUE(player.pause());
    ASSERT_TRUE(eventually([&output] { return !output->running(); }));

    player.seek(1_s);

    ASSERT_TRUE(eventually([moved, &player] {
        return moved->num_seeks() == 1 && player.time_played() != 1_s;
    }));
    EXPECT_EQ(player.state(), State::paused);
    EXPECT_FALSE(player.finished());

    player.stop();
    player.wait();
}

TEST(Player, PublishesTheAchievedPositionOfAnApproximateSeek)
{
    const StreamSpec spec{units::Frequency{100.0}, 2};
    auto source{std::make_unique<wiola::testing::FakeDecoder>(wiola::pcm::Frames{10000}, spec)};
    wiola::testing::FakeDecoder* moved{source.get()};
    moved->achieve_next_seek_at(wiola::pcm::Frames{90});

    wiola::pcm::PcmQueue buffer{spec, wiola::pcm::Frames{512}};
    wiola::core::Lease<wiola::testing::FakeOutput> output =
        wiola::core::Lease<wiola::testing::FakeOutput>::make();
    Player player{std::move(source), buffer.producer(), output};

    ASSERT_TRUE(player.start());
    ASSERT_TRUE(player.pause());
    ASSERT_TRUE(eventually([&output] { return !output->running(); }));

    player.seek(1_s);

    ASSERT_TRUE(eventually([moved] { return moved->num_seeks() == 1; }));
    EXPECT_EQ(player.time_played(), units::Time{0.9});
    EXPECT_EQ(player.state(), State::paused);

    player.stop();
    player.wait();
}

TEST(Player, NewerSeekSurvivesFailureOfAnOlderOne)
{
    const StreamSpec spec{units::Frequency{100.0}, 2};
    auto source{std::make_unique<wiola::testing::FakeDecoder>(wiola::pcm::Frames{10000}, spec)};
    wiola::testing::FakeDecoder* moved{source.get()};
    moved->refuse_next_seek();

    wiola::testing::Gate seeking;
    moved->block_seeks_on(seeking);

    wiola::pcm::PcmQueue buffer{StreamSpec{}, wiola::pcm::Frames{512}};
    wiola::core::Lease<wiola::testing::FakeOutput> output =
        wiola::core::Lease<wiola::testing::FakeOutput>::make();
    Player player{std::move(source), buffer.producer(), output};

    ASSERT_TRUE(player.start());
    ASSERT_TRUE(player.pause());
    ASSERT_TRUE(eventually([&output] { return !output->running(); }));

    player.seek(1_s);
    seeking.wait_until_entered();
    player.seek(2_s);
    seeking.release();

    ASSERT_TRUE(eventually([moved] {
        return moved->num_seeks() == 2 && moved->last_seek() == wiola::pcm::Frames{200};
    }));
    EXPECT_EQ(player.time_played(), 2_s);
    EXPECT_EQ(player.state(), State::paused);

    player.stop();
    player.wait();
}

TEST(Player, TimePlayedStartsAtTheBeginning)
{
    auto source = pcm_source();

    ASSERT_NE(source, nullptr);

    Rig rig{std::move(source)};
    Player& player{rig.player};

    EXPECT_EQ(player.time_played(), units::Time{});
}

TEST(Player, TimePlayedReachesTheEndOfTheSource)
{
    auto source = pcm_source();

    ASSERT_NE(source, nullptr);

    const auto duration{source->spec().time_per(source->num_frames())};
    Rig rig{std::move(source)};
    Player& player{rig.player};

    ASSERT_TRUE(player.start());

    rig.play_out();
    player.wait();

    EXPECT_EQ(player.time_played(), duration);
}

/// Position follows the device, so a seek moves it even though nothing has been played since.
TEST(Player, TimePlayedFollowsASeek)
{
    auto source = pcm_source();

    ASSERT_NE(source, nullptr);

    Rig rig{std::move(source)};
    Player& player{rig.player};

    ASSERT_TRUE(player.start());

    ASSERT_TRUE(player.pause());

    player.seek(200_ms);

    EXPECT_TRUE(eventually([&player] {
        return player.time_played().get<wiola::units::Sec>() > 0.19;
    }));
    EXPECT_LT(player.time_played().get<wiola::units::Sec>(), 0.22);
}

TEST(Player, ResumeRefusesBeforeStart)
{
    auto source = pcm_source();

    ASSERT_NE(source, nullptr);

    Rig rig{std::move(source)};
    Player& player{rig.player};

    EXPECT_EQ(player.state(), State::idle);
    EXPECT_FALSE(player.resume());
    EXPECT_FALSE(player.playing());
}

/// A source that ran out and a listener who pressed stop are both final, and a playlist has to
/// tell them apart: one is the cue to play the next thing, the other is not.
TEST(Player, DistinguishesEndingFromBeingStopped)
{
    auto ended = pcm_source();

    ASSERT_NE(ended, nullptr);

    Rig first_rig{std::move(ended)};
    Player& first{first_rig.player};

    ASSERT_TRUE(first.start());

    first_rig.play_out();
    first.wait();
    EXPECT_EQ(first.state(), State::ended);
    EXPECT_TRUE(first.finished());

    auto stopped = pcm_source();
    Rig second_rig{std::move(stopped)};
    Player& second{second_rig.player};

    ASSERT_TRUE(second.start());

    second.stop();
    second.wait();

    EXPECT_EQ(second.state(), State::stopped);
    EXPECT_TRUE(second.finished());
}

TEST(Player, TimePlayedReportsASeekThatHasNotBeenAppliedYet)
{
    auto source = pcm_source();

    ASSERT_NE(source, nullptr);

    Rig rig{std::move(source)};
    Player& player{rig.player};

    // No device is needed: nothing has started, so nothing can apply the request.
    player.seek(1000_ms);

    EXPECT_NEAR(player.time_played().get<units::Sec>(), 1.0, 0.01);
}

TEST(Player, StartsWhereASeekAskedForBeforeIt)
{
    auto source = pcm_source();

    ASSERT_NE(source, nullptr);

    const wiola::pcm::Frames total{source->num_frames()};
    const auto* begun{source.get()};
    Rig rig{std::move(source)};
    Player& player{rig.player};

    player.seek(1000_ms);

    ASSERT_TRUE(player.start());

    EXPECT_GT(player.time_played().get<units::Sec>(), 0.9);

    ASSERT_TRUE(eventually([&] { return begun->num_seeks() > 0 && rig.output->running(); }));

    // Inspect the decoder cursor only after the worker has joined.
    player.stop();
    player.wait();

    // Beginning a second into the source left the decoder with only the rest of it to read.
    EXPECT_LT(begun->num_frames_left(), wiola::pcm::Frames{total.count() / 2});
}

TEST(Player, KeepsASeekAskedForAfterStopping)
{
    auto source = pcm_source();

    ASSERT_NE(source, nullptr);

    Rig rig{std::move(source)};
    Player& player{rig.player};

    ASSERT_TRUE(player.start());

    player.stop();
    player.wait();

    // A stopped player has no thread to take the request up, so start is what honors it.
    player.seek(1000_ms);
    EXPECT_NEAR(player.time_played().get<units::Sec>(), 1.0, 0.01);

    ASSERT_TRUE(player.start());
    EXPECT_GT(player.time_played().get<units::Sec>(), 0.9);
}

/// A source that holds a seek until the test has inspected the outstanding intent.
class SlowSource final : public wiola::media::AudioDecoder {
public:
    explicit SlowSource(wiola::testing::Gate& gate)
        : wiola::media::AudioDecoder{
              wiola::pcm::StreamSpec{units::Frequency{44100.0}, 2},
              wiola::pcm::Frames{44100 * 4}
    },
        gate_{gate}
    {
    }

protected:
    wiola::media::Result<wiola::media::DecodeOutcome> decode(std::span<float> output,
        wiola::pcm::Frames num_frames) override
    {
        std::fill_n(output.begin(), num_frames.count() * 2, 0.0F);

        return wiola::media::DecodeOutcome{.frames = num_frames};
    }

    wiola::media::Result<wiola::media::SeekOutcome>
    seek_frame(wiola::pcm::Frames frame_index) override
    {
        gate_.wait();

        return wiola::media::SeekOutcome{.target = frame_index, .achieved = frame_index};
    }

private:
    wiola::testing::Gate& gate_;
};

/// A source short enough to run out while what it decoded is still being played, and one that
/// says how often it was moved.
class ShortSource final : public wiola::media::AudioDecoder {
public:
    ShortSource()
        : wiola::media::AudioDecoder{
              wiola::pcm::StreamSpec{units::Frequency{44100.0}, 2},
              wiola::pcm::Frames{4410}
    }
    {
    }

    [[nodiscard]] int num_seeks() const noexcept { return num_seeks_.load(); }

protected:
    wiola::media::Result<wiola::media::DecodeOutcome> decode(std::span<float> output,
        wiola::pcm::Frames num_frames) override
    {
        std::fill_n(output.begin(), num_frames.count() * 2, 0.0F);

        return wiola::media::DecodeOutcome{.frames = num_frames};
    }

    wiola::media::Result<wiola::media::SeekOutcome>
    seek_frame(wiola::pcm::Frames frame_index) override
    {
        num_seeks_.fetch_add(1);

        return wiola::media::SeekOutcome{.target = frame_index, .achieved = frame_index};
    }

private:
    std::atomic<int> num_seeks_{0};
};

/// The source runs out long before the device has played what it decoded. A seek asked for in
/// that gap is carried out there and then, rather than waiting for the next start.
TEST(Player, SeeksWhileTheLastOfItIsStillBeingPlayed)
{
    auto source = std::make_unique<ShortSource>();
    const ShortSource* moved{source.get()};

    const StreamSpec spec{source->spec()};
    wiola::pcm::PcmQueue buffer{spec,
        *spec.checked_frames_at(units::Time{std::chrono::milliseconds{250}})};

    // An output that plays nothing holds the player in that gap for as long as the test needs.
    wiola::core::Lease<wiola::testing::FakeOutput> output =
        wiola::core::Lease<wiola::testing::FakeOutput>::make();
    Player player{std::move(source), buffer.producer(), output};

    ASSERT_TRUE(player.start());
    ASSERT_EQ(moved->num_seeks(), 0);

    player.seek(50_ms);

    EXPECT_TRUE(eventually([moved] { return moved->num_seeks() == 1; }));
    EXPECT_FALSE(player.finished());

    player.stop();
    player.wait();
}

/// Carrying a seek out takes time - a device to stop, a file to move through - and for all of it
/// the place asked for is where playback is going. Reporting the old one meanwhile shows as a bar
/// that springs back before it settles.
TEST(Player, TimePlayedNeverFallsBackWhileASeekIsCarriedOut)
{
    wiola::testing::Gate gate;
    Rig rig{std::make_unique<SlowSource>(gate)};
    Player& player{rig.player};

    struct ReleaseOnExit {
        wiola::testing::Gate& gate;

        ~ReleaseOnExit() { gate.release(); }
    } release{gate};

    ASSERT_TRUE(player.start());
    player.seek(2000_ms);
    gate.wait_until_entered();

    for (int sample = 0; sample < 64; ++sample)
        EXPECT_GT(player.time_played().get<units::Sec>(), 1.9);

    gate.release();
    EXPECT_TRUE(eventually([&] { return player.time_played().get<units::Sec>() >= 2.0; }));
}

TEST(Player, PlaysAgainAfterEnding)
{
    auto source = pcm_source();

    ASSERT_NE(source, nullptr);

    Rig rig{std::move(source)};
    Player& player{rig.player};

    ASSERT_TRUE(player.start());

    rig.play_out();
    player.wait();
    ASSERT_EQ(player.state(), State::ended);

    // Starting a finished player winds it back rather than refusing.
    rig.hold_output();
    ASSERT_TRUE(player.start());
    EXPECT_EQ(player.state(), State::playing);
    EXPECT_LT(player.time_played().get<wiola::units::Sec>(), 0.5);

    rig.play_out();
    player.wait();
    EXPECT_EQ(player.state(), State::ended);
}

TEST(Player, RefusesToStartWhilePlaying)
{
    auto source = pcm_source();

    ASSERT_NE(source, nullptr);

    Rig rig{std::move(source)};
    Player& player{rig.player};

    ASSERT_TRUE(player.start());

    EXPECT_FALSE(player.start());
    EXPECT_EQ(player.state(), State::playing);

    ASSERT_TRUE(player.pause());
    EXPECT_FALSE(player.start());
    EXPECT_EQ(player.state(), State::paused);
}

/// A player needs somewhere to play, not a sound card: with an output of its own playback
/// runs the same on a machine that has none.
TEST(Player, RunsOnAnyOutput)
{
    auto source = pcm_source();

    ASSERT_NE(source, nullptr);

    const StreamSpec spec{source->spec()};
    wiola::pcm::PcmQueue buffer{spec,
        *spec.checked_frames_at(units::Time{std::chrono::milliseconds{250}})};
    wiola::core::Lease<wiola::testing::FakeOutput> output =
        wiola::core::Lease<wiola::testing::FakeOutput>::make();
    Player player{std::move(source), buffer.producer(), output};

    ASSERT_TRUE(player.start());
    EXPECT_EQ(player.state(), State::playing);
    EXPECT_TRUE(eventually([&output] { return output->running(); }));

    player.stop();
    player.wait();

    EXPECT_EQ(player.state(), State::stopped);
    EXPECT_FALSE(output->running());
}

/// What has been heard is the output's count, not what has been decoded.
TEST(Player, FollowsTheOutputRatherThanTheDecoder)
{
    auto source = pcm_source();

    ASSERT_NE(source, nullptr);

    const StreamSpec spec{source->spec()};
    wiola::pcm::PcmQueue buffer{spec,
        *spec.checked_frames_at(units::Time{std::chrono::milliseconds{250}})};
    wiola::core::Lease<wiola::testing::FakeOutput> output =
        wiola::core::Lease<wiola::testing::FakeOutput>::make();
    Player player{std::move(source), buffer.producer(), output};

    ASSERT_TRUE(player.start());
    ASSERT_TRUE(eventually([&output] { return output->running(); }));
    EXPECT_EQ(player.time_played().get<units::Sec>(), 0.0);

    output->play(wiola::pcm::Frames{
        static_cast<std::size_t>(spec.sample_rate.get<units::Hz>()) / 2});

    EXPECT_NEAR(player.time_played().get<units::Sec>(), 0.5, 1e-6);

    player.stop();
    player.wait();
}

/// Everything a listener can ask for, as fast as it can be asked, against a track that is
/// playing. The tests above each name one interleaving; this one names none, and is there for
/// the ones nobody thought to name. It asserts almost nothing on its own - the thread sanitizer
/// is what reads the result.
TEST(Player, SurvivesATransportHammering)
{
    auto source = pcm_source();

    ASSERT_NE(source, nullptr);

    Rig rig{std::move(source)};
    Player& player{rig.player};

    ASSERT_TRUE(player.start());

    const std::jthread listener{[&player] {
        for (int i = 0; i < 2048; ++i) {
            static_cast<void>(player.pause());
            player.seek(units::Time{std::chrono::milliseconds{i % 1400}});
            static_cast<void>(player.resume());
        }
    }};
}

/// The rule the device depends on: the caller opens it, and from the moment there is a decoding
/// thread, only that thread starts or stops it. A listener asking to pause moves the state and
/// nothing else. Checked here rather than left to a sanitizer, so it holds on any build.
TEST(Player, TouchesTheOutputFromTheDecodingThreadAlone)
{
    auto source = pcm_source();

    ASSERT_NE(source, nullptr);

    const StreamSpec spec{source->spec()};
    wiola::pcm::PcmQueue buffer{spec,
        *spec.checked_frames_at(units::Time{std::chrono::milliseconds{250}})};
    wiola::core::Lease<wiola::testing::FakeOutput> output =
        wiola::core::Lease<wiola::testing::FakeOutput>::make();
    Player player{std::move(source), buffer.producer(), output};

    ASSERT_TRUE(player.start());

    // The caller opened the device on the line above; everything from here is the decoding
    // thread's, and the pauses below are what would tempt another thread into it.
    output->watch();

    for (int i = 0; i < 20; ++i) {
        ASSERT_TRUE(player.pause());

        ASSERT_TRUE(eventually([&] { return !output->running(); }));

        ASSERT_TRUE(player.resume());
        ASSERT_TRUE(eventually([&] { return output->running(); }));
    }

    player.stop();
    player.wait();

    const std::vector<std::thread::id> touching{output->touching_threads()};

    ASSERT_EQ(touching.size(), 1u) << "more than one thread started or stopped the output";
    EXPECT_NE(touching.front(), std::this_thread::get_id());
}

/// A device that will not start again is a fault, not a listener pressing stop: the two look the
/// same to a window that can only read the state, and only one of them is worth saying.
TEST(Player, ReportsAFaultWhenTheOutputWillNotStartAgain)
{
    auto source = pcm_source();

    ASSERT_NE(source, nullptr);

    const StreamSpec spec{source->spec()};
    wiola::pcm::PcmQueue buffer{spec,
        *spec.checked_frames_at(units::Time{std::chrono::milliseconds{250}})};
    wiola::core::Lease<wiola::testing::FakeOutput> output =
        wiola::core::Lease<wiola::testing::FakeOutput>::make();
    Player player{std::move(source), buffer.producer(), output};

    ASSERT_TRUE(player.start());
    ASSERT_TRUE(player.pause());

    // The device is silenced by the decoding thread, so the refusal is set once it has been:
    // otherwise a pause and a resume can both land before that thread looks.
    ASSERT_TRUE(eventually([&output] { return !output->running(); }));

    output->refuse_starts();

    ASSERT_TRUE(player.resume());

    EXPECT_TRUE(eventually([&player] { return player.finished(); }));
    EXPECT_EQ(player.state(), State::faulted);

    player.stop();
    player.wait();
}

/// A device that stops answering while a track plays is noticed, rather than left to look like
/// playback that carries on with nothing coming out.
TEST(Player, ReportsAFaultWhenTheDeviceStopsOnItsOwn)
{
    auto source = pcm_source();

    ASSERT_NE(source, nullptr);

    const StreamSpec spec{source->spec()};
    wiola::pcm::PcmQueue buffer{spec,
        *spec.checked_frames_at(units::Time{std::chrono::milliseconds{250}})};
    wiola::core::Lease<wiola::testing::FakeOutput> output =
        wiola::core::Lease<wiola::testing::FakeOutput>::make();
    Player player{std::move(source), buffer.producer(), output};

    ASSERT_TRUE(player.start());
    ASSERT_TRUE(player.playing());
    ASSERT_TRUE(eventually([&output] { return output->running(); }));

    output->die();

    EXPECT_TRUE(eventually([&player] { return player.finished(); }))
        << "playback carried on with a device that had stopped";
    EXPECT_EQ(player.state(), State::faulted);

    player.stop();
    player.wait();
}
