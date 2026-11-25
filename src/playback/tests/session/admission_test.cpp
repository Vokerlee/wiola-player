/**
 * @file
 * @brief Session resource admission, supplied processing and candidate rollback contracts.
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

#include <allocation_failure.hpp>
#include <fakes/decoder.hpp>
#include <fakes/output.hpp>
#include <fakes/provider.hpp>
#include <fakes/resource.hpp>
#include <gtest/gtest.h>
#include <wiola/audio/processing_chain.hpp>
#include <wiola/audio/stage.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#include <thread>
#include <utility>
#include <variant>

namespace {

using namespace wiola::units::literals;
using wiola::engine::OpenResult;
using wiola::engine::Session;
using wiola::pcm::Frames;
using wiola::testing::session::load_and_wait;
using wiola::testing::session::poll_until;
using wiola::testing::session::SessionFixture;
using State = wiola::engine::PlaybackState;
namespace units = wiola::units;

/// A supplied processing implementation independent of the built-in equalizer and topology.
class SuppliedProcessing final : public wiola::audio::Stage {
public:
    std::unique_ptr<Preparation> prepare(wiola::pcm::StreamSpec spec) noexcept override
    {
        prepared_for = spec;
        return nullptr;
    }

    void process(std::span<float> samples) noexcept override
    {
        for (auto& sample : samples)
            sample += 0.25F;
    }

    std::optional<wiola::pcm::StreamSpec> prepared_for;
};

TEST(Session, PreparesAndRendersThroughTheHostSuppliedProcessingPort)
{
    SuppliedProcessing processing;
    wiola::audio::Source* rendered{};
    wiola::testing::FakeOutput* device{};
    Session session{processing,
        [&](wiola::audio::Source& source) {
            rendered = &source;
            auto output{std::make_unique<wiola::testing::FakeOutput>()};
            device = output.get();
            return output;
        },
        std::make_shared<wiola::testing::FakeMediaProvider>(
            [](const wiola::media::ResourceReference&, const wiola::media::OperationContext&) {
                return wiola::media::DecoderOpenResult{
                    std::make_unique<wiola::testing::FakeDecoder>()};
            },
            [](const wiola::media::ResourceReference&, const wiola::media::OperationContext&) {
                return wiola::media::Metadata{};
            }
        )};
    ASSERT_EQ(load_and_wait(session,
                  wiola::testing::resource("supplied-processing", "supplied-processing")),
        OpenResult::opened);
    ASSERT_TRUE(processing.prepared_for);
    EXPECT_EQ(*processing.prepared_for, wiola::pcm::StreamSpec{});
    ASSERT_TRUE(session.play());
    ASSERT_TRUE(poll_until(session, [&] { return device->running(); }));
    std::array<float, 2> block{};
    EXPECT_EQ(rendered->render(block), block.size());
    EXPECT_EQ(block, (std::array{0.25F, 0.25F}));
}

TEST(Session, RejectsAnInvalidStreamBeforeBuildingAPipeline)
{
    std::size_t num_outputs{0};
    wiola::audio::ProcessingChain processing;
    Session session{processing,
        [&num_outputs](wiola::audio::Source&) {
            ++num_outputs;
            return std::make_unique<wiola::testing::FakeOutput>();
        },
        std::make_shared<wiola::testing::FakeMediaProvider>(
            [](const wiola::media::ResourceReference&, const wiola::media::OperationContext&) {
                auto decoder{std::make_unique<wiola::testing::FakeDecoder>(Frames{64},
                    wiola::pcm::StreamSpec{.sample_rate = units::Frequency{48000.0},
                        .num_channels = 0})};
                return wiola::media::DecoderOpenResult{std::move(decoder)};
            },
            [](const wiola::media::ResourceReference&, const wiola::media::OperationContext&) {
                return wiola::media::Metadata{};
            }
        )};

    EXPECT_EQ(session.open({wiola::testing::resource("invalid.wav", "invalid")}),
        OpenResult::loading);
    while (session.reading())
        std::this_thread::yield();
    session.catch_up();

    EXPECT_EQ(session.open_result(), OpenResult::damaged);
    EXPECT_FALSE(session.loaded());
    EXPECT_EQ(num_outputs, 0U);
}

TEST(Session, OwnsIndependentAndValidatedBufferingConfiguration)
{
    const auto loads_with = [](units::Time duration) {
        wiola::audio::ProcessingChain processing;
        Session session{processing,
            [](wiola::audio::Source&) { return std::make_unique<wiola::testing::FakeOutput>(); },
            std::make_shared<wiola::testing::FakeMediaProvider>(
                [](const wiola::media::ResourceReference&, const wiola::media::OperationContext&) {
                    return wiola::media::DecoderOpenResult{
                        std::make_unique<wiola::testing::FakeDecoder>()};
                },
                [](const wiola::media::ResourceReference&, const wiola::media::OperationContext&) {
                    return wiola::media::Metadata{};
                }
            ),
            wiola::engine::TransportConfig{}, wiola::engine::BufferingConfig{.duration = duration}};

        session.open({wiola::testing::resource("track.wav", "track")});
        while (session.reading())
            std::this_thread::yield();
        session.catch_up();

        return session.loaded();
    };

    EXPECT_TRUE(loads_with(10_ms));
    EXPECT_TRUE(loads_with(20_ms));
    EXPECT_THROW(loads_with(units::Time{}), std::invalid_argument);
}

TEST(Session, RejectsInvalidConstructionBeforeStartingAnyCollaborator)
{
    wiola::audio::ProcessingChain processing;
    std::atomic<unsigned> media_calls{};
    unsigned output_calls{};
    const auto provider{std::make_shared<wiola::testing::FakeMediaProvider>(
        [&](const wiola::media::ResourceReference&, const wiola::media::OperationContext&) {
            ++media_calls;
            return wiola::media::DecoderOpenResult{std::make_unique<wiola::testing::FakeDecoder>()};
        },
        [&](const wiola::media::ResourceReference&, const wiola::media::OperationContext&) {
            ++media_calls;
            return wiola::media::Metadata{};
        }
    )};
    const auto construct{
        [&](std::shared_ptr<wiola::media::MediaProvider> media,
            wiola::engine::TransportConfig transport, wiola::engine::BufferingConfig buffering) {
            return std::make_unique<Session>(
                processing,
                [&](wiola::audio::Source&) {
                    ++output_calls;
                    return std::make_unique<wiola::testing::FakeOutput>();
                },
                std::move(media), transport, buffering
            );
        }};
    EXPECT_THROW(construct({}, {}, {}), std::invalid_argument);
    wiola::engine::TransportConfig invalid;
    invalid.decode_chunk = wiola::pcm::Frames{};
    EXPECT_THROW(construct(provider, invalid, {}), std::invalid_argument);
    invalid = {};
    invalid.poll_interval = std::chrono::milliseconds{};
    EXPECT_THROW(construct(provider, invalid, {}), std::invalid_argument);
    EXPECT_THROW(construct(provider, {}, wiola::engine::BufferingConfig{.duration = units::Time{}}),
        std::invalid_argument);
    EXPECT_EQ(media_calls, 0U);
    EXPECT_EQ(output_calls, 0U);
}

/// A device is opened for a format, and a track in the same one plays through the device that is
/// already open: a track change that closed and opened a device would be heard.
TEST(Session, KeepsTheDeviceAcrossTracksOfTheSameFormat)
{
    SessionFixture fixture;
    const wiola::media::ResourceReference resource{fixture.track()};

    ASSERT_EQ(load_and_wait(fixture.session, resource), OpenResult::opened);
    ASSERT_EQ(load_and_wait(fixture.session, resource), OpenResult::opened);

    EXPECT_EQ(fixture.num_outputs, 1U);
}

/// A format it cannot play is a device it has to open again: what is open was cut for the rate
/// of the track before.
TEST(Session, OpensAnotherDeviceWhenTheFormatChanges)
{
    SessionFixture fixture;

    ASSERT_EQ(load_and_wait(fixture.session, fixture.track()), OpenResult::opened);
    ASSERT_EQ(fixture.num_outputs, 1U);

    ASSERT_EQ(load_and_wait(fixture.session,
                  fixture.track(wiola::pcm::StreamSpec{.sample_rate = units::Frequency{44100.0},
                      .num_channels = 2})),
        OpenResult::opened);

    EXPECT_EQ(fixture.num_outputs, 2U);
}

/// A setting belongs to the listener, not to what is playing.
TEST(Session, PreservesSuppliedProcessingSettingsAcrossTracks)
{
    SessionFixture fixture;
    const wiola::media::ResourceReference resource{fixture.track()};

    fixture.processing.volume.set_gain(0.25F);
    fixture.processing.equalizer.set_band_gain(3, 6.0F);

    ASSERT_EQ(load_and_wait(fixture.session, resource), OpenResult::opened);

    EXPECT_FLOAT_EQ(fixture.processing.volume.gain(), 0.25F);
    EXPECT_FLOAT_EQ(fixture.processing.equalizer.band_gain(3), 6.0F);
}

TEST(Session, OutputSetupFailurePreservesActiveUnseekablePlayback)
{
    constexpr wiola::pcm::StreamSpec current_spec{.sample_rate = units::Frequency{48000.0},
        .num_channels = 2};
    constexpr wiola::pcm::StreamSpec next_spec{.sample_rate = units::Frequency{22050.0},
        .num_channels = 8};
    enum class Fault {
        no_output,
        exception,
        unknown_exception,
    };
    for (const auto fault : {Fault::no_output, Fault::exception, Fault::unknown_exception}) {
        wiola::testing::FakeOutput* active_device{};
        bool fail{false};
        wiola::testing::Shaping processing{wiola::pcm::StreamSpec{}};
        Session session{processing.chain,
            [&](wiola::audio::Source&) -> std::unique_ptr<wiola::audio::Output> {
                if (fail) {
                    if (fault == Fault::no_output)
                        return nullptr;
                    if (fault == Fault::exception)
                        throw std::runtime_error{"controlled device failure"};
                    throw fault;
                }
                auto output{std::make_unique<wiola::testing::FakeOutput>()};
                active_device = output.get();
                return output;
            },
            std::make_shared<wiola::testing::FakeMediaProvider>(
                [&](const wiola::media::ResourceReference& resource,
                    const wiola::media::OperationContext&) -> wiola::media::DecoderOpenResult {
                    if (resource == wiola::testing::resource("current", "current"))
                        return wiola::media::DecoderOpenResult{
                            std::make_unique<wiola::testing::FakeDecoder>(
                                wiola::media::DecoderStreamInfo{.spec = current_spec,
                                    .length = std::nullopt,
                                    .seekable = false}
                            )};
                    return wiola::media::DecoderOpenResult{
                        std::make_unique<wiola::testing::FakeDecoder>(Frames{64}, next_spec)};
                },
                [](const wiola::media::ResourceReference&, const wiola::media::OperationContext&) {
                    return wiola::media::Metadata{};
                }
            )};
        ASSERT_EQ(load_and_wait(session, wiola::testing::resource("current", "current")),
            OpenResult::opened);
        processing.equalizer.set_band_gain(9, 3.0F);
        processing.volume.set_gain(0.25F);
        ASSERT_TRUE(session.play());
        ASSERT_TRUE(poll_until(session, [&] { return active_device->running(); }));
        active_device->play(Frames{32});
        const auto position{session.time_played()};
        static_cast<void>(session.take_outcomes());
        fail = true;
        ASSERT_EQ(session.open({wiola::testing::resource("candidate", "candidate")}),
            OpenResult::loading);
        const auto request{session.open_request_id()};
        while (session.reading())
            std::this_thread::yield();

        EXPECT_NO_THROW(session.catch_up());

        ASSERT_TRUE(session.loaded());
        EXPECT_EQ(session.open_result(), OpenResult::output_unavailable);
        EXPECT_EQ(session.track(), wiola::testing::resource("current", "current"));
        EXPECT_TRUE(session.playing());
        EXPECT_FALSE(session.seekable());
        EXPECT_TRUE(active_device->running());
        EXPECT_EQ(session.time_played(), position);
        EXPECT_EQ(processing.equalizer.num_bands(), 10U);
        EXPECT_FLOAT_EQ(processing.equalizer.preamp(), -3.0F);
        EXPECT_FLOAT_EQ(processing.volume.gain(), 0.25F);
        const auto outcomes{session.take_outcomes()};
        const auto completion{std::ranges::find_if(outcomes.values, [&](const auto& value) {
            return value.operation == wiola::engine::OperationKind::open && value.id == request;
        })};
        ASSERT_NE(completion, outcomes.values.end());
        ASSERT_TRUE(completion->error);
        const auto* error{std::get_if<wiola::engine::OutputSetupError>(&*completion->error)};
        ASSERT_NE(error, nullptr);
        EXPECT_FALSE(error->detail.empty());
        if (fault == Fault::exception) {
            EXPECT_EQ(error->detail, "controlled device failure");
        }
        fail = false;
        EXPECT_EQ(load_and_wait(session, wiola::testing::resource("candidate", "candidate")),
            OpenResult::opened);
        EXPECT_EQ(session.track(), wiola::testing::resource("candidate", "candidate"));
        EXPECT_EQ(processing.equalizer.num_bands(), 9U);
    }
}

TEST(Session, NullDecoderCompletionReportsFailureWithoutOpeningOutput)
{
    std::size_t outputs{0};
    wiola::audio::ProcessingChain processing;
    Session session{processing,
        [&](wiola::audio::Source&) {
            ++outputs;
            return std::make_unique<wiola::testing::FakeOutput>();
        },
        std::make_shared<wiola::testing::FakeMediaProvider>(
            [](const wiola::media::ResourceReference&, const wiola::media::OperationContext&) {
                return wiola::media::DecoderOpenResult{
                    std::unique_ptr<wiola::media::AudioDecoder>{}};
            },
            [](const wiola::media::ResourceReference&, const wiola::media::OperationContext&) {
                return wiola::media::Metadata{};
            }
        )};
    EXPECT_EQ(load_and_wait(session, wiola::testing::resource("empty-decoder", "empty-decoder")),
        OpenResult::damaged);
    EXPECT_FALSE(session.loaded());
    EXPECT_EQ(outputs, 0U);
    const auto outcomes{session.take_outcomes()};
    const auto opened{std::ranges::find_if(outcomes.values,
        [](const auto& value) { return value.operation == wiola::engine::OperationKind::open; })};
    ASSERT_NE(opened, outcomes.values.end());
    ASSERT_TRUE(opened->error);
    EXPECT_EQ(std::get<wiola::media::Error>(*opened->error).category,
        wiola::media::ErrorCategory::damaged);
}

TEST(Session, InvalidStreamCompletionCannotReportASuccessfulOpen)
{
    wiola::audio::ProcessingChain processing;
    Session session{processing, wiola::testing::passive_output,
        std::make_shared<wiola::testing::FakeMediaProvider>(
            [](const wiola::media::ResourceReference&, const wiola::media::OperationContext&) {
                return wiola::media::DecoderOpenResult{
                    std::make_unique<wiola::testing::FakeDecoder>(Frames{64},
                        wiola::pcm::StreamSpec{.sample_rate = units::Frequency{},
                            .num_channels = 2})};
            },
            [](const wiola::media::ResourceReference&, const wiola::media::OperationContext&) {
                return wiola::media::Metadata{};
            }
        )};
    ASSERT_EQ(load_and_wait(session, wiola::testing::resource("invalid-stream", "invalid-stream")),
        OpenResult::damaged);
    const auto outcomes{session.take_outcomes()};
    std::size_t open_completions{0};
    for (const auto& completion : outcomes.values) {
        if (completion.operation != wiola::engine::OperationKind::open)
            continue;
        ++open_completions;
        ASSERT_TRUE(completion.error);
        EXPECT_EQ(std::get<wiola::media::Error>(*completion.error).category,
            wiola::media::ErrorCategory::damaged);
    }
    EXPECT_EQ(open_completions, 1U);
}

TEST(Session, InvalidDecoderCapabilitiesCannotRetireTheActivePipeline)
{
    SuppliedProcessing processing;
    std::size_t outputs{0};
    Session session{processing,
        [&](wiola::audio::Source&) {
            ++outputs;
            return std::make_unique<wiola::testing::FakeOutput>();
        },
        std::make_shared<wiola::testing::FakeMediaProvider>(
            [](const wiola::media::ResourceReference& resource,
                const wiola::media::OperationContext&) {
                if (resource == wiola::testing::resource("invalid.media", "invalid"))
                    return wiola::media::DecoderOpenResult{
                        std::make_unique<wiola::testing::FakeDecoder>(
                            wiola::media::DecoderStreamInfo{
                                                            .spec = {.sample_rate = units::Frequency{48000.0},
                                    .num_channels = 1},
                                                            .length = std::nullopt,
                                                            .length_accuracy = wiola::media::LengthAccuracy::exact}
                        )
                    };
                return wiola::media::DecoderOpenResult{
                    std::make_unique<wiola::testing::FakeDecoder>()};
            },
            [](const wiola::media::ResourceReference&, const wiola::media::OperationContext&) {
                return wiola::media::Metadata{};
            }
        )};
    ASSERT_EQ(load_and_wait(session, wiola::testing::resource("active.media", "active")),
        OpenResult::opened);
    ASSERT_TRUE(session.play());
    const auto prepared{processing.prepared_for};
    static_cast<void>(session.take_outcomes());

    EXPECT_EQ(load_and_wait(session, wiola::testing::resource("invalid.media", "invalid")),
        OpenResult::damaged);

    EXPECT_EQ(outputs, 1U);
    EXPECT_EQ(processing.prepared_for, prepared);
    EXPECT_TRUE(session.loaded());
    EXPECT_TRUE(session.playing());
    EXPECT_EQ(session.track(), wiola::testing::resource("active.media", "active"));
    const auto outcomes{session.take_outcomes()};
    const auto opened{std::ranges::find_if(outcomes.values,
        [](const auto& value) { return value.operation == wiola::engine::OperationKind::open; })};
    ASSERT_NE(opened, outcomes.values.end());
    ASSERT_TRUE(opened->error);
    const auto* error{std::get_if<wiola::media::Error>(&*opened->error)};
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(error->category, wiola::media::ErrorCategory::damaged);
    EXPECT_EQ(error->detail, "exact decoder length is missing");
}

TEST(Session, UnrepresentableBufferPoliciesFailBeforePreparingResources)
{
    const wiola::pcm::StreamSpec spec;
    const std::array durations{units::Time{std::numeric_limits<double>::min()},
        units::Time{std::numeric_limits<double>::max()},
        units::Time{static_cast<double>(std::numeric_limits<std::size_t>::max()) /
            spec.sample_rate.hz() / static_cast<double>(spec.num_channels)}};
    for (const auto duration : durations) {
        SuppliedProcessing processing;
        std::size_t outputs{0};
        Session session{processing,
            [&](wiola::audio::Source&) {
                ++outputs;
                return std::make_unique<wiola::testing::FakeOutput>();
            },
            std::make_shared<wiola::testing::FakeMediaProvider>(
                [](const wiola::media::ResourceReference&, const wiola::media::OperationContext&) {
                    return wiola::media::DecoderOpenResult{
                        std::make_unique<wiola::testing::FakeDecoder>()};
                },
                [](const wiola::media::ResourceReference&, const wiola::media::OperationContext&) {
                    return wiola::media::Metadata{};
                }
            ),
            {}, {.duration = duration}};

        EXPECT_EQ(load_and_wait(session,
                      wiola::testing::resource("controlled.media", "controlled")),
            OpenResult::damaged);
        EXPECT_FALSE(session.loaded());
        EXPECT_FALSE(processing.prepared_for);
        EXPECT_EQ(outputs, 0U);
        const auto outcomes{session.take_outcomes()};
        const auto opened{std::ranges::find_if(outcomes.values, [](const auto& value) {
            return value.operation == wiola::engine::OperationKind::open;
        })};
        ASSERT_NE(opened, outcomes.values.end());
        ASSERT_TRUE(opened->error);
        const auto* error{std::get_if<wiola::media::Error>(&*opened->error)};
        ASSERT_NE(error, nullptr);
        EXPECT_EQ(error->category, wiola::media::ErrorCategory::damaged);
        EXPECT_EQ(error->detail, "decoder stream cannot support the configured playback buffer");
    }
}

TEST(Session, DeviceStartFailureIsAnEventualTransportFailureAfterInstallation)
{
    wiola::audio::ProcessingChain processing;
    Session session{processing,
        [](wiola::audio::Source&) {
            auto output{std::make_unique<wiola::testing::FakeOutput>()};
            output->refuse_starts();
            return output;
        },
        std::make_shared<wiola::testing::FakeMediaProvider>(
            [](const wiola::media::ResourceReference&, const wiola::media::OperationContext&) {
                return wiola::media::DecoderOpenResult{
                    std::make_unique<wiola::testing::FakeDecoder>()};
            },
            [](const wiola::media::ResourceReference&, const wiola::media::OperationContext&) {
                return wiola::media::Metadata{};
            }
        )};
    ASSERT_EQ(load_and_wait(session, wiola::testing::resource("installed", "installed")),
        OpenResult::opened);
    ASSERT_TRUE(session.play());
    ASSERT_TRUE(poll_until(session, [&] { return session.state() == State::faulted; }));
    EXPECT_TRUE(session.loaded());
    EXPECT_EQ(session.track(), wiola::testing::resource("installed", "installed"));
    EXPECT_EQ(session.open_result(), OpenResult::opened);
    const auto outcomes{session.take_outcomes()};
    std::size_t opened{0};
    for (const auto& completion : outcomes.values) {
        if (completion.operation == wiola::engine::OperationKind::open) {
            ++opened;
            EXPECT_FALSE(completion.error);
        }
    }
    EXPECT_EQ(opened, 1U);
}

#if WIOLA_TEST_ALLOCATOR_REPLACEMENT
TEST(Session, EveryInstallationAllocationFailurePreservesActiveUnseekablePlayback)
{
    constexpr wiola::pcm::StreamSpec current_spec{.sample_rate = units::Frequency{48000.0},
        .num_channels = 2};
    constexpr wiola::pcm::StreamSpec next_spec{.sample_rate = units::Frequency{22050.0},
        .num_channels = 8};
    bool observed_failure{false};
    for (std::size_t after{0};; ++after) {
        wiola::testing::Gate opening;
        wiola::testing::FakeOutput* active_device{};
        wiola::testing::Shaping processing{wiola::pcm::StreamSpec{}};
        Session session{processing.chain,
            [&](wiola::audio::Source& rendered) {
                auto output{std::make_unique<wiola::testing::FakeOutput>()};
                if (rendered.spec() == current_spec)
                    active_device = output.get();
                return output;
            },
            std::make_shared<wiola::testing::FakeMediaProvider>(
                [&](const wiola::media::ResourceReference& resource,
                    const wiola::media::OperationContext&) -> wiola::media::DecoderOpenResult {
                    if (resource == wiola::testing::resource("current", "current"))
                        return wiola::media::DecoderOpenResult{
                            std::make_unique<wiola::testing::FakeDecoder>(
                                wiola::media::DecoderStreamInfo{.spec = current_spec,
                                    .length = std::nullopt,
                                    .seekable = false}
                            )};
                    opening.wait();
                    return wiola::media::DecoderOpenResult{
                        std::make_unique<wiola::testing::FakeDecoder>(Frames{64}, next_spec)};
                },
                [](const wiola::media::ResourceReference&, const wiola::media::OperationContext&) {
                    wiola::media::Metadata tags;
                    tags.track.title = "settled";
                    return tags;
                }
            )};
        ASSERT_EQ(load_and_wait(session, wiola::testing::resource("current", "current")),
            OpenResult::opened);
        ASSERT_TRUE(poll_until(session,
            [&] { return session.playlist().tracks().front().metadata().title == "settled"; }));
        processing.equalizer.set_band_gain(9, 3.0F);
        processing.volume.set_gain(0.25F);
        ASSERT_TRUE(session.play());
        ASSERT_TRUE(poll_until(session, [&] { return active_device->running(); }));
        active_device->play(Frames{32});
        const auto position{session.time_played()};
        ASSERT_EQ(session.open({wiola::testing::resource("candidate", "candidate")}),
            OpenResult::loading);
        opening.wait_until_entered();
        // Settle unrelated metadata before injecting failures into the installation turn.
        const bool metadata_settled{poll_until(session,
            [&] { return session.playlist().tracks().front().metadata().title == "settled"; })};
        opening.release();
        ASSERT_TRUE(metadata_settled);
        static_cast<void>(session.take_outcomes());
        while (session.reading())
            std::this_thread::yield();
        const auto request{session.open_request_id()};
        bool failed{false};
        {
            const wiola::testing::AllocationFailure failure{after};
            try {
                session.catch_up();
            } catch (const std::bad_alloc&) {
                failed = true;
            }
        }
        const auto outcomes{session.take_outcomes()};
        const auto opened{std::ranges::find_if(outcomes.values, [&](const auto& value) {
            return value.operation == wiola::engine::OperationKind::open && value.id == request;
        })};
        if (!failed) {
            EXPECT_TRUE(observed_failure);
            EXPECT_EQ(session.track(), wiola::testing::resource("candidate", "candidate"));
            EXPECT_EQ(session.open_result(), OpenResult::opened);
            ASSERT_NE(opened, outcomes.values.end());
            EXPECT_FALSE(opened->error);
            break;
        }
        observed_failure = true;
        EXPECT_EQ(opened, outcomes.values.end());
        ASSERT_TRUE(session.loaded());
        EXPECT_EQ(session.track(), wiola::testing::resource("current", "current"));
        EXPECT_EQ(session.state(), State::playing);
        EXPECT_TRUE(active_device->running());
        EXPECT_FALSE(session.seekable());
        EXPECT_EQ(session.time_played(), position);
        EXPECT_EQ(processing.equalizer.num_bands(), 10U);
        EXPECT_FLOAT_EQ(processing.equalizer.preamp(), -3.0F);
        EXPECT_FLOAT_EQ(processing.equalizer.band_gain(9), 3.0F);
        EXPECT_FLOAT_EQ(processing.volume.gain(), 0.25F);
    }
}
#endif

} // namespace
