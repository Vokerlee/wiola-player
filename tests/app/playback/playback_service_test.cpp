/**
 * @file
 * @brief Application command tests for PlaybackService.
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

#include <app/playback/playback_service.hpp>
#include <fakes/resource.hpp>

#include <fakes/provider.hpp>

#include <fakes/decoder.hpp>
#include <fakes/output.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

namespace {

using wiola::app::PlaybackService;
using wiola::engine::CommandResult;

PlaybackService idle_service()
{
    return PlaybackService{wiola::testing::passive_output,
        std::make_shared<wiola::testing::FakeMediaProvider>(
            [](const wiola::media::ResourceReference&,
                const wiola::media::OperationContext&) -> wiola::media::DecoderOpenResult {
                ADD_FAILURE() << "an idle session must not open media";
                return wiola::media::unexpected{
                    wiola::media::Error{.category = wiola::media::ErrorCategory::unreadable,
                                        .detail = "unexpected idle open"}
                };
            },
            [](const wiola::media::ResourceReference&, const wiola::media::OperationContext&) {
                ADD_FAILURE() << "an idle session must not read metadata";
                return wiola::media::Metadata{};
            }
        )};
}

TEST(PlaybackService, ReportsUnavailableCommandsWithoutLoadedMedia)
{
    auto service{idle_service()};

    EXPECT_EQ(service.play(), CommandResult::unavailable);
    EXPECT_EQ(service.pause(), CommandResult::unavailable);
    EXPECT_EQ(service.resume(), CommandResult::unavailable);
}

TEST(PlaybackService, SeparatesPlayPauseAndResume)
{
    PlaybackService service{
        [](wiola::audio::Source&) { return std::make_unique<wiola::testing::FakeOutput>(); },
        std::make_shared<wiola::testing::FakeMediaProvider>(
            [](const wiola::media::ResourceReference&,
                const wiola::media::OperationContext&) -> wiola::media::DecoderOpenResult {
                std::unique_ptr<wiola::media::AudioDecoder> decoder{
                    std::make_unique<wiola::testing::FakeDecoder>()};
                return decoder;
            },
            [](const wiola::media::ResourceReference&, const wiola::media::OperationContext&) {
                return wiola::media::Metadata{};
            }
        )};

    service.open({wiola::testing::resource("track.wav", "track")});
    while (service.reading())
        std::this_thread::yield();
    static_cast<void>(service.pump());
    ASSERT_TRUE(service.loaded());

    EXPECT_EQ(service.play(), CommandResult::accepted);
    EXPECT_EQ(service.state(), wiola::engine::PlaybackState::playing);
    EXPECT_TRUE(service.playing());
    EXPECT_TRUE(service.seekable());
    EXPECT_EQ(service.track(), wiola::testing::resource("track.wav", "track"));
    EXPECT_EQ(service.time_played(), wiola::units::Time{});
    EXPECT_EQ(service.pause(), CommandResult::accepted);
    EXPECT_EQ(service.state(), wiola::engine::PlaybackState::paused);
    EXPECT_FALSE(service.playing());
    EXPECT_EQ(service.resume(), CommandResult::accepted);
    EXPECT_EQ(service.state(), wiola::engine::PlaybackState::playing);
    EXPECT_TRUE(service.playing());
    EXPECT_EQ(service.toggle_playback(), CommandResult::accepted);
    EXPECT_EQ(service.state(), wiola::engine::PlaybackState::paused);
    EXPECT_EQ(service.toggle_playback(), CommandResult::accepted);
    EXPECT_EQ(service.state(), wiola::engine::PlaybackState::playing);
    service.stop();
}

TEST(PlaybackService, ExposesAudioControlsAsValues)
{
    auto service{idle_service()};

    service.set_volume(0.25F);
    service.set_equalizer_enabled(true);
    ASSERT_GT(service.num_equalizer_bands(), 0U);
    service.set_equalizer_band(0, 3.0F);

    EXPECT_FLOAT_EQ(service.volume(), 0.25F);
    EXPECT_TRUE(service.equalizer_enabled());
    EXPECT_FLOAT_EQ(service.equalizer_band(0), 3.0F);
}

} // namespace

TEST(PlaybackService, HeadlessPumpPublishesACoherentCompletionSnapshot)
{
    PlaybackService service{
        [](wiola::audio::Source&) { return std::make_unique<wiola::testing::FakeOutput>(); },
        std::make_shared<wiola::testing::FakeMediaProvider>(
            [](const wiola::media::ResourceReference&,
                const wiola::media::OperationContext&) -> wiola::media::DecoderOpenResult {
                std::unique_ptr<wiola::media::AudioDecoder> decoder{
                    std::make_unique<wiola::testing::FakeDecoder>()};
                return decoder;
            },
            [](const wiola::media::ResourceReference&, const wiola::media::OperationContext&) {
                return wiola::media::Metadata{};
            }
        )};

    service.open({wiola::testing::resource("headless.wav", "headless")});
    while (service.reading())
        std::this_thread::yield();

    const auto snapshot{service.pump()};
    EXPECT_TRUE(snapshot.loaded);
    EXPECT_EQ(snapshot.open_result, wiola::engine::OpenResult::opened);
    EXPECT_EQ(snapshot.track, wiola::testing::resource("headless.wav", "headless"));
    EXPECT_EQ(snapshot.queue_position, 0U);
    EXPECT_EQ(snapshot.total_time, service.total_time());
}

TEST(PlaybackService, ConnectsAnAcceptedOpenWithItsTypedFailure)
{
    PlaybackService service{
        [](wiola::audio::Source&) { return std::make_unique<wiola::testing::FakeOutput>(); },
        std::make_shared<wiola::testing::FakeMediaProvider>(
            [](const wiola::media::ResourceReference&, const wiola::media::OperationContext&) {
                return wiola::media::DecoderOpenResult{wiola::media::unexpected{
                    wiola::media::Error{.category = wiola::media::ErrorCategory::unsupported,
                        .detail = "missing codec"}}};
            },
            [](const wiola::media::ResourceReference&, const wiola::media::OperationContext&) {
                return wiola::media::Metadata{};
            }
        )};

    const auto receipt{service.open({wiola::testing::resource("unsupported.wav", "unsupported")})};
    ASSERT_EQ(receipt.acceptance, CommandResult::accepted);
    ASSERT_NE(receipt.id, 0U);

    while (service.reading())
        std::this_thread::yield();
    service.pump();

    const auto outcomes{service.take_outcomes()};
    const auto outcome{std::ranges::find_if(outcomes.values, [&receipt](const auto& value) {
        return value.operation == wiola::engine::OperationKind::open && value.id == receipt.id;
    })};

    ASSERT_NE(outcome, outcomes.values.end());
    ASSERT_TRUE(outcome->error);
    EXPECT_EQ(std::get<wiola::media::Error>(*outcome->error).category,
        wiola::media::ErrorCategory::unsupported);
    EXPECT_EQ(std::get<wiola::media::Error>(*outcome->error).detail, "missing codec");
    EXPECT_TRUE(service.take_outcomes().values.empty());
}

TEST(PlaybackService, SuppressesAnObsoleteOpenCompletion)
{
    wiola::testing::Gate opening;
    PlaybackService service{
        [](wiola::audio::Source&) { return std::make_unique<wiola::testing::FakeOutput>(); },
        std::make_shared<wiola::testing::FakeMediaProvider>(
            [&opening](const wiola::media::ResourceReference& resource,
                const wiola::media::OperationContext&) -> wiola::media::DecoderOpenResult {
                if (resource.key() == "old.wav") {
                    opening.wait();
                    return wiola::media::unexpected{
                        wiola::media::Error{.category = wiola::media::ErrorCategory::unreadable,
                                            .detail = "old"}
                    };
                }

                return wiola::media::DecoderOpenResult{
                    std::make_unique<wiola::testing::FakeDecoder>()};
            },
            [](const wiola::media::ResourceReference&, const wiola::media::OperationContext&) {
                return wiola::media::Metadata{};
            }
        )};

    const auto old{service.open({wiola::testing::resource("old.wav", "old")})};
    opening.wait_until_entered();
    const auto current{service.open({wiola::testing::resource("current.wav", "current")})};
    opening.release();

    while (service.reading())
        std::this_thread::yield();
    service.pump();

    const auto outcomes{service.take_outcomes()};
    std::size_t open_outcomes{0};

    for (const auto& outcome : outcomes.values) {
        if (outcome.operation != wiola::engine::OperationKind::open)
            continue;

        ++open_outcomes;
        EXPECT_EQ(outcome.id, current.id);
        EXPECT_NE(outcome.id, old.id);
        EXPECT_FALSE(outcome.error);
    }

    EXPECT_EQ(open_outcomes, 1U);
    EXPECT_EQ(service.open_result(), wiola::engine::OpenResult::opened);
}

TEST(PlaybackService, ReportsMetadataFailureWithoutSilencingPlayback)
{
    std::atomic<std::size_t> num_metadata_reads{0};
    PlaybackService service{
        [](wiola::audio::Source&) { return std::make_unique<wiola::testing::FakeOutput>(); },
        std::make_shared<wiola::testing::FakeMediaProvider>(
            [](const wiola::media::ResourceReference&,
                const wiola::media::OperationContext&) -> wiola::media::DecoderOpenResult {
                return wiola::media::DecoderOpenResult{
                    std::make_unique<wiola::testing::FakeDecoder>()};
            },
            [&num_metadata_reads](
                const wiola::media::ResourceReference&, const wiola::media::OperationContext&
            ) -> wiola::media::Result<wiola::media::Metadata> {
                ++num_metadata_reads;
                return wiola::media::unexpected{
                    wiola::media::Error{.category = wiola::media::ErrorCategory::damaged,
                                        .detail = "bad tags"}
                };
            }
        )};

    const auto receipt{service.open({wiola::testing::resource("playable.wav", "playable")})};
    ASSERT_EQ(receipt.acceptance, CommandResult::accepted);

    while (!service.loaded() || service.playlist().metadata_revision() == 0) {
        service.pump();
        std::this_thread::yield();
    }

    const auto outcomes{service.take_outcomes()};
    const auto metadata{std::ranges::find_if(outcomes.values, [](const auto& value) {
        return value.operation == wiola::engine::OperationKind::metadata;
    })};

    ASSERT_NE(metadata, outcomes.values.end());
    ASSERT_TRUE(metadata->error);
    EXPECT_EQ(std::get<wiola::media::Error>(*metadata->error).category,
        wiola::media::ErrorCategory::damaged);
    EXPECT_EQ(std::get<wiola::media::Error>(*metadata->error).detail, "bad tags");
    EXPECT_TRUE(service.loaded());
    EXPECT_EQ(service.open_result(), wiola::engine::OpenResult::opened);
}

TEST(PlaybackService, BoundsRetainedOutcomesAndReportsOverflow)
{
    std::atomic<std::size_t> num_metadata_reads{0};
    PlaybackService service{
        [](wiola::audio::Source&) { return std::make_unique<wiola::testing::FakeOutput>(); },
        std::make_shared<wiola::testing::FakeMediaProvider>(
            [](const wiola::media::ResourceReference&,
                const wiola::media::OperationContext&) -> wiola::media::DecoderOpenResult {
                return wiola::media::DecoderOpenResult{
                    std::make_unique<wiola::testing::FakeDecoder>()};
            },
            [&num_metadata_reads](const wiola::media::ResourceReference&,
                const wiola::media::OperationContext&) {
                ++num_metadata_reads;
                return wiola::media::Metadata{};
            }
        )};

    std::vector<wiola::media::ResourceReference> tracks;

    for (std::size_t index{0}; index < 80; ++index)
        tracks.push_back(wiola::testing::resource("track-" + std::to_string(index), "Track"));

    service.open(std::move(tracks));

    while (service.reading() || num_metadata_reads < 80)
        std::this_thread::yield();
    service.pump();

    const auto outcomes{service.take_outcomes()};
    const auto opens{std::ranges::count_if(outcomes.values, [](const auto& outcome) {
        return outcome.operation == wiola::engine::OperationKind::open;
    })};
    const auto metadata{std::ranges::count_if(outcomes.values, [](const auto& outcome) {
        return outcome.operation == wiola::engine::OperationKind::metadata;
    })};

    EXPECT_EQ(outcomes.values.size(), 64U);
    EXPECT_TRUE(outcomes.overflowed)
        << "open outcomes: " << opens << ", metadata outcomes: " << metadata
        << ", open result: " << static_cast<int>(service.open_result());

    const auto next{service.take_outcomes()};
    EXPECT_TRUE(next.values.empty());
    EXPECT_FALSE(next.overflowed);
}

TEST(PlaybackService, IndependentServicesKeepTheirOwnProcessingSettings)
{
    auto first{idle_service()};
    auto second{idle_service()};
    first.set_volume(0.25F);
    first.set_equalizer_band(0, 3.0F);
    first.set_equalizer_enabled(false);

    EXPECT_FLOAT_EQ(second.volume(), 1.0F);
    EXPECT_FLOAT_EQ(second.equalizer_band(0), 0.0F);
    EXPECT_TRUE(second.equalizer_enabled());
    EXPECT_FLOAT_EQ(first.volume(), 0.25F);
    EXPECT_FLOAT_EQ(first.equalizer_band(0), 3.0F);
    EXPECT_FALSE(first.equalizer_enabled());
}

TEST(PlaybackService, DestructionQuiescesActiveCallbacksBeforeItsProcessingStorage)
{
    wiola::testing::FakeOutput* device{};
    auto service{std::make_unique<PlaybackService>(
        [&](wiola::audio::Source& source) {
            auto output{std::make_unique<wiola::testing::FakeOutput>(source)};
            device = output.get();
            return output;
        },
        std::make_shared<wiola::testing::FakeMediaProvider>(
            [](const wiola::media::ResourceReference&, const wiola::media::OperationContext&) {
                return wiola::media::DecoderOpenResult{
                    std::make_unique<wiola::testing::FakeDecoder>(
                        wiola::media::DecoderStreamInfo{.spec = {}, .length = std::nullopt}
                    )};
            },
            [](const wiola::media::ResourceReference&, const wiola::media::OperationContext&) {
                return wiola::media::Metadata{};
            }
        )
    )};
    service->open({wiola::testing::resource("continuous", "Continuous")});
    while (service->reading())
        std::this_thread::yield();
    static_cast<void>(service->pump());
    ASSERT_TRUE(service->loaded());
    ASSERT_EQ(service->play(), CommandResult::accepted);
    const auto deadline{std::chrono::steady_clock::now() + std::chrono::seconds{5}};
    while (device->frames_played() == wiola::pcm::Frames{} &&
        std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();
    ASSERT_GT(device->frames_played(), wiola::pcm::Frames{});
    service.reset();
}

TEST(PlaybackService, ShutdownRejectsNewCommandsWithoutChangingAcceptedQueueOrSettings)
{
    PlaybackService service{wiola::testing::passive_output,
        std::make_shared<wiola::testing::FakeMediaProvider>(
            [](const wiola::media::ResourceReference&, const wiola::media::OperationContext&) {
                return wiola::media::DecoderOpenResult{
                    std::make_unique<wiola::testing::FakeDecoder>()};
            },
            [](const wiola::media::ResourceReference&, const wiola::media::OperationContext&) {
                return wiola::media::Metadata{};
            }
        )};
    service.open({wiola::testing::resource("first", "first"),
        wiola::testing::resource("selected", "selected"),
        wiola::testing::resource("last", "last")});
    while (service.reading())
        std::this_thread::yield();
    static_cast<void>(service.pump());
    ASSERT_TRUE(service.play_track(1));
    while (service.reading())
        std::this_thread::yield();
    static_cast<void>(service.pump());
    ASSERT_TRUE(service.loaded());
    ASSERT_TRUE(service.total_time());
    const auto seek_position{*service.total_time() * 0.5};
    service.set_volume(0.25F);
    service.set_equalizer_band(0, 3.0F);
    service.set_repeat(wiola::engine::RepeatMode::all);
    const auto before{service.snapshot()};

    service.stop_accepting();

    const auto receipt{service.open({wiola::testing::resource("late", "late")})};
    EXPECT_EQ(receipt.acceptance, CommandResult::unavailable);
    EXPECT_EQ(receipt.id, 0U);
    service.add({wiola::testing::resource("late", "late")});
    EXPECT_FALSE(service.remove(0));
    service.clear();
    EXPECT_FALSE(service.play_track(0));
    EXPECT_FALSE(service.next_track());
    EXPECT_FALSE(service.previous_track());
    EXPECT_EQ(service.play(), CommandResult::unavailable);
    EXPECT_EQ(service.pause(), CommandResult::unavailable);
    EXPECT_EQ(service.resume(), CommandResult::unavailable);
    service.seek(seek_position);
    service.set_volume(0.75F);
    service.set_equalizer_enabled(false);
    service.set_equalizer_band(0, -3.0F);
    service.set_repeat(wiola::engine::RepeatMode::none);
    service.cycle_repeat();
    service.shuffle(true);
    service.stop();

    EXPECT_EQ(service.playlist().tracks().size(), 3U);
    EXPECT_EQ(service.playlist().position(), 1U);
    EXPECT_EQ(service.track(), before.track);
    EXPECT_EQ(service.snapshot().structural_revision, before.structural_revision);
    EXPECT_EQ(service.time_played(), before.time_played);
    EXPECT_FLOAT_EQ(service.volume(), 0.25F);
    EXPECT_TRUE(service.equalizer_enabled());
    EXPECT_FLOAT_EQ(service.equalizer_band(0), 3.0F);
    EXPECT_EQ(service.repeat(), wiola::engine::RepeatMode::all);
    EXPECT_FALSE(service.shuffled());
    EXPECT_TRUE(service.playback_extensions().empty());
}

TEST(PlaybackService, MediaHandoffRestoresOnlyWhileTheApplicationStillAcceptsWork)
{
    std::atomic<unsigned> opens{0};
    PlaybackService service{wiola::testing::passive_output,
        std::make_shared<wiola::testing::FakeMediaProvider>(
            [&](const wiola::media::ResourceReference&, const wiola::media::OperationContext&) {
                ++opens;
                return wiola::media::DecoderOpenResult{
                    std::make_unique<wiola::testing::FakeDecoder>()};
            },
            [](const wiola::media::ResourceReference&, const wiola::media::OperationContext&) {
                return wiola::media::Metadata{};
            }
        )};
    service.open({wiola::testing::resource("editable", "editable")});
    while (service.reading())
        std::this_thread::yield();
    static_cast<void>(service.pump());
    ASSERT_TRUE(service.loaded());
    EXPECT_FALSE(service.release_media(wiola::testing::resource("unrelated", "unrelated")));
    const auto first{service.release_media(wiola::testing::resource("editable", "editable"))};
    ASSERT_TRUE(first);
    EXPECT_FALSE(service.loaded());
    service.restore_media(*first);
    while (service.reading())
        std::this_thread::yield();
    static_cast<void>(service.pump());
    EXPECT_TRUE(service.loaded());
    EXPECT_EQ(opens, 2U);

    service.stop_accepting();
    const auto retiring{service.release_media(wiola::testing::resource("editable", "editable"))};
    ASSERT_TRUE(retiring);
    service.restore_media(*retiring);
    static_cast<void>(service.pump());
    EXPECT_FALSE(service.loaded());
    EXPECT_FALSE(service.reading());
    EXPECT_EQ(opens, 2U);
}

TEST(PlaybackService, FractionSeekLeavesAnUnknownLengthStreamUntouched)
{
    PlaybackService service{wiola::testing::passive_output,
        std::make_shared<wiola::testing::FakeMediaProvider>(
            [](const wiola::media::ResourceReference&, const wiola::media::OperationContext&) {
                return wiola::media::DecoderOpenResult{
                    std::make_unique<wiola::testing::FakeDecoder>(
                        wiola::media::DecoderStreamInfo{.spec = {}, .length = std::nullopt}
                    )};
            },
            [](const wiola::media::ResourceReference&, const wiola::media::OperationContext&) {
                return wiola::media::Metadata{};
            }
        )};
    service.open({wiola::testing::resource("unknown-length", "unknown-length")});
    while (service.reading())
        std::this_thread::yield();
    static_cast<void>(service.pump());
    ASSERT_TRUE(service.loaded());
    ASSERT_TRUE(service.seekable());
    EXPECT_FALSE(service.total_time());

    service.seek_to(0.5);

    EXPECT_EQ(service.time_played(), wiola::units::Time{});
    EXPECT_EQ(service.track(), wiola::testing::resource("unknown-length", "unknown-length"));
}
