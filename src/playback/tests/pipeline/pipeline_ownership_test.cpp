/**
 * @file
 * @brief Standalone playback pipeline and output lifetime tests.
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

#include <allocation_failure.hpp>
#include <fakes/decoder.hpp>
#include <fakes/output.hpp>
#include <pipeline/output_session.hpp>
#include <pipeline/playback_pipeline.hpp>
#include <wiola/audio/processing_chain.hpp>
#include <wiola/audio/volume.hpp>
#include <wiola/media/stream_spec.hpp>
#include <wiola/units.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>

namespace {

using wiola::audio::ProcessingChain;
using wiola::engine::OutputSession;
using wiola::engine::PlaybackPipeline;
using wiola::pcm::Frames;
using wiola::pcm::StreamSpec;
using namespace wiola::units;

constexpr StreamSpec stereo{.sample_rate = 48_kHz, .num_channels = 2};
constexpr StreamSpec mono{.sample_rate = 44100_Hz, .num_channels = 1};

class ConstantSource final : public wiola::audio::Source {
public:
    explicit ConstantSource(StreamSpec spec = stereo) noexcept
        : spec_{spec}
    {
    }

    StreamSpec spec() const noexcept override { return spec_; }

    std::size_t render(std::span<float> samples) noexcept override
    {
        const auto count{spec_.samples_per(spec_.frames_per(samples.size()))};
        std::fill_n(samples.begin(), count, 0.25F);

        return count;
    }

private:
    StreamSpec spec_;
};

struct Lifetime {
    wiola::audio::Source* bridge{};
    wiola::testing::FakeOutput* device{};
    std::atomic<bool> started{false};
    bool output_destroyed{false};
    bool decoder_destroyed{false};
    bool decoder_saw_live_output{false};
    bool decoder_saw_running_output{false};
};

// Compose the existing controlled device with a lifetime witness; no second device simulator.
class ObservedOutput final : public wiola::audio::Output {
public:
    ObservedOutput(wiola::audio::Source& source, Lifetime& lifetime)
        : lifetime_{lifetime}
        , device_{source}
        , control_{device_.control()}
    {
        lifetime_.bridge = &source;
        lifetime_.device = &device_;
    }

    ~ObservedOutput() override
    {
        control_.stop();
        lifetime_.output_destroyed = true;
    }

    bool running() const noexcept override { return device_.running(); }

    Frames frames_played() const noexcept override { return device_.frames_played(); }

private:
    bool start() noexcept override
    {
        const bool started{control_.start()};
        lifetime_.started.store(started, std::memory_order_release);
        lifetime_.started.notify_one();

        return started;
    }

    void stop() noexcept override { control_.stop(); }

    Lifetime& lifetime_;
    wiola::testing::FakeOutput device_;
    wiola::audio::Output::Control control_;
};

class ObservedDecoder final : public wiola::media::AudioDecoder {
public:
    explicit ObservedDecoder(Lifetime& lifetime)
        : AudioDecoder{
              wiola::media::DecoderStreamInfo{.spec = stereo, .length = std::nullopt}
    },
        lifetime_{lifetime}
    {
    }

    ~ObservedDecoder() override
    {
        lifetime_.decoder_destroyed = true;
        lifetime_.decoder_saw_live_output = !lifetime_.output_destroyed;

        if (!lifetime_.output_destroyed)
            lifetime_.decoder_saw_running_output = lifetime_.device->running();
    }

private:
    wiola::media::Result<wiola::media::DecodeOutcome> decode(std::span<float> samples,
        Frames frames) override
    {
        std::fill_n(samples.begin(), spec().samples_per(frames), 0.0F);
        return wiola::media::DecodeOutcome{.frames = frames};
    }

    wiola::media::Result<wiola::media::SeekOutcome> seek_frame(Frames target) override
    {
        return wiola::media::SeekOutcome{.target = target, .achieved = target};
    }

    Lifetime& lifetime_;
};

TEST(OutputSession, RenderBridgeAcceptsAStageWithoutRequiringAChain)
{
    wiola::audio::Volume processing;
    processing.set_gain(0.5F);

    ConstantSource source;
    Lifetime lifetime;
    OutputSession output{processing, [&](wiola::audio::Source& rendered) {
                             return std::make_unique<ObservedOutput>(rendered, lifetime);
                         }};
    auto lease{output.prepare(stereo)};
    output.activate(*lease, source);

    std::array<float, 2> samples{};

    EXPECT_EQ(lifetime.bridge->render(samples), samples.size());
    EXPECT_EQ(samples, (std::array{0.125F, 0.125F}));
}

TEST(OutputSession, ReusesOnlyTheCommittedStreamSpec)
{
    ProcessingChain chain;
    ConstantSource source;
    ConstantSource mono_source{mono};
    std::size_t num_outputs{0};
    OutputSession session{chain, [&num_outputs](wiola::audio::Source&) {
                              ++num_outputs;
                              return std::make_unique<wiola::testing::FakeOutput>();
                          }};
    auto first{session.prepare(stereo)};

    EXPECT_FALSE(session.open_for(stereo));

    session.activate(*first, source);

    auto same{session.prepare(stereo)};

    EXPECT_EQ(&same->output(), &first->output());
    EXPECT_EQ(num_outputs, 1U);

    auto different{session.prepare(mono)};

    EXPECT_TRUE(session.open_for(stereo));
    EXPECT_FALSE(session.open_for(mono));

    session.activate(*different, mono_source);
    EXPECT_TRUE(session.open_for(mono));
    EXPECT_EQ(num_outputs, 2U);
}

TEST(OutputSession, NullOrThrowingFactoryPreservesTheCommittedOutputAndSource)
{
    ProcessingChain chain;
    ConstantSource source;
    wiola::audio::Source* bridge{nullptr};
    enum class Failure {
        none,
        no_output,
        exception,
    };

    Failure failure{Failure::none};
    OutputSession session{chain,
        [&](wiola::audio::Source& rendered) -> std::unique_ptr<wiola::audio::Output> {
            if (failure == Failure::no_output)
                return nullptr;

            if (failure == Failure::exception)
                throw std::runtime_error{"device construction failed"};

            bridge = &rendered;

            return std::make_unique<wiola::testing::FakeOutput>();
        }};
    auto active{session.prepare(stereo)};
    session.activate(*active, source);

    const auto* original{&active->output()};

    for (const auto fault : {Failure::no_output, Failure::exception}) {
        failure = fault;
        EXPECT_THROW(static_cast<void>(session.prepare(mono)), std::runtime_error);
        EXPECT_TRUE(session.open_for(stereo));
        EXPECT_FALSE(session.open_for(mono));
        EXPECT_EQ(&active->output(), original);

        std::array<float, 2> samples{};

        ASSERT_NE(bridge, nullptr);
        EXPECT_EQ(bridge->render(samples), samples.size());
        EXPECT_EQ(samples, (std::array{0.25F, 0.25F}));
    }
}

TEST(OutputSession, DiscardedSameFormatCandidateCannotDetachTheActiveSource)
{
    ProcessingChain chain;
    ConstantSource source;
    Lifetime lifetime;
    OutputSession session{chain, [&](wiola::audio::Source& rendered) {
                              return std::make_unique<ObservedOutput>(rendered, lifetime);
                          }};
    auto active{session.prepare(stereo)};
    session.activate(*active, source);
    {
        const auto discarded{session.prepare(stereo)};
    }

    std::array<float, 2> samples{};

    EXPECT_EQ(lifetime.bridge->render(samples), samples.size());

    active.reset();
    EXPECT_EQ(lifetime.bridge->render(samples), 0U);
    EXPECT_FALSE(lifetime.output_destroyed);
}

TEST(OutputSession, SupersededLeaseCannotDetachTheSuccessorEvenForTheSameSource)
{
    ProcessingChain chain;
    ConstantSource source;
    Lifetime lifetime;
    OutputSession session{chain, [&](wiola::audio::Source& rendered) {
                              return std::make_unique<ObservedOutput>(rendered, lifetime);
                          }};
    auto first{session.prepare(stereo)};
    session.activate(*first, source);

    auto successor{session.prepare(stereo)};
    session.activate(*successor, source);
    first.reset();

    auto owner{std::move(successor)};
    std::array<float, 2> samples{};

    EXPECT_EQ(lifetime.bridge->render(samples), samples.size());

    owner.reset();
    EXPECT_EQ(lifetime.bridge->render(samples), 0U);
}

TEST(OutputSession, LeasePinsItsOutputAcrossCacheReplacementAndCacheDestruction)
{
    ProcessingChain chain;
    ConstantSource source;
    ConstantSource mono_source{mono};
    Lifetime old_stream;
    Lifetime new_stream;
    std::unique_ptr<OutputSession::Lease> old;
    {
        OutputSession session{chain, [&](wiola::audio::Source& rendered) {
                                  return std::make_unique<ObservedOutput>(rendered,
                                      rendered.spec() == stereo ? old_stream : new_stream);
                              }};
        old = session.prepare(stereo);
        session.activate(*old, source);

        auto next{session.prepare(mono)};
        session.activate(*next, mono_source);
        EXPECT_FALSE(old_stream.output_destroyed);
        EXPECT_FALSE(new_stream.output_destroyed);
    }
    EXPECT_FALSE(old_stream.output_destroyed);
    EXPECT_TRUE(new_stream.output_destroyed);

    {
        auto control{old->output().control()};

        EXPECT_TRUE(control.start());

        control.stop();
    }
    old.reset();
    EXPECT_TRUE(old_stream.output_destroyed);
}

#if WIOLA_TEST_ALLOCATOR_REPLACEMENT
TEST(OutputSession, EveryPreparationAllocationFailurePreservesTheCommittedConnection)
{
    bool observed_failure{false};
    for (std::size_t after{0};; ++after) {
        ProcessingChain chain;
        ConstantSource source;
        wiola::audio::Source* bridge{nullptr};
        OutputSession session{chain, [&](wiola::audio::Source& rendered) {
                                  if (rendered.spec() == stereo)
                                      bridge = &rendered;
                                  return std::make_unique<wiola::testing::FakeOutput>();
                              }};
        auto active{session.prepare(stereo)};
        session.activate(*active, source);

        std::unique_ptr<OutputSession::Lease> candidate;
        {
            const wiola::testing::AllocationFailure failure{after};
            try {
                candidate = session.prepare(mono);
            } catch (const std::bad_alloc&) {
                observed_failure = true;
            }
        }
        EXPECT_TRUE(session.open_for(stereo));
        EXPECT_FALSE(session.open_for(mono));

        std::array<float, 2> samples{};

        ASSERT_NE(bridge, nullptr);
        EXPECT_EQ(bridge->render(samples), samples.size());

        if (candidate) {
            EXPECT_TRUE(observed_failure);
            break;
        }
    }
}
#endif

TEST(PlaybackPipeline, OwnsAPlayableTrackWithoutASession)
{
    ProcessingChain chain;
    OutputSession output{chain,
        [](wiola::audio::Source&) { return std::make_unique<wiola::testing::FakeOutput>(); }};
    auto source{std::make_unique<wiola::testing::FakeDecoder>(Frames{64}, stereo)};
    PlaybackPipeline pipeline{std::move(source), output.prepare(stereo), Frames{64}};
    pipeline.activate(output);
    ASSERT_TRUE(pipeline.player().start());

    pipeline.player().stop();
    pipeline.player().wait();
    EXPECT_TRUE(pipeline.player().finished());
}

TEST(PlaybackPipeline, FailedCandidateConstructionDoesNotDetachTheActiveConnection)
{
    ProcessingChain chain;
    ConstantSource source;
    Lifetime lifetime;
    OutputSession output{chain, [&](wiola::audio::Source& rendered) {
                             return std::make_unique<ObservedOutput>(rendered, lifetime);
                         }};
    auto active{output.prepare(stereo)};
    output.activate(*active, source);

    wiola::engine::TransportConfig invalid;
    invalid.decode_chunk = Frames{};
    EXPECT_THROW((PlaybackPipeline{
                     std::make_unique<wiola::testing::FakeDecoder>(Frames{64}, stereo),
                     output.prepare(stereo), Frames{64}, invalid}),
        std::invalid_argument);

    std::array<float, 2> samples{};

    EXPECT_EQ(lifetime.bridge->render(samples), samples.size());
}

TEST(PlaybackPipeline, PreparedSameFormatReplacementAcquiresTheReturnedOutput)
{
    ProcessingChain chain;
    wiola::testing::FakeOutput* device{};
    OutputSession output{chain, [&](wiola::audio::Source&) {
                             auto result{std::make_unique<wiola::testing::FakeOutput>()};
                             device = result.get();
                             result->refuse_starts();

                             return result;
                         }};
    auto active{
        std::make_unique<PlaybackPipeline>(std::make_unique<wiola::testing::FakeDecoder>(Frames{64},
                                               stereo),
            output.prepare(stereo), Frames{64})};
    active->activate(output);

    PlaybackPipeline candidate{std::make_unique<wiola::testing::FakeDecoder>(Frames{64}, stereo),
        output.prepare(stereo), Frames{64}};

    active.reset();
    candidate.activate(output);
    ASSERT_TRUE(candidate.player().start());

    candidate.player().wait();
    EXPECT_EQ(device->num_starts(), 1);
    EXPECT_EQ(candidate.player().state(), wiola::engine::PlaybackState::faulted);
}

TEST(PlaybackPipeline, QuiescesAndReleasesDecoderBeforeItsPinnedOutput)
{
    ProcessingChain chain;
    Lifetime lifetime;
    std::unique_ptr<PlaybackPipeline> pipeline;
    {
        OutputSession output{chain, [&](wiola::audio::Source& rendered) {
                                 return std::make_unique<ObservedOutput>(rendered, lifetime);
                             }};
        pipeline = std::make_unique<PlaybackPipeline>(std::make_unique<ObservedDecoder>(lifetime),
            output.prepare(stereo), Frames{64});
        pipeline->activate(output);
    }
    EXPECT_FALSE(lifetime.output_destroyed);
    ASSERT_TRUE(pipeline->player().start());

    lifetime.started.wait(false, std::memory_order_acquire);
    pipeline.reset();
    EXPECT_TRUE(lifetime.decoder_destroyed);
    EXPECT_TRUE(lifetime.decoder_saw_live_output);
    EXPECT_FALSE(lifetime.decoder_saw_running_output);
    EXPECT_TRUE(lifetime.output_destroyed);
}

} // namespace
