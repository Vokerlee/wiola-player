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

#include <app/playback_service.hpp>

#include <fakes/decoder.hpp>
#include <fakes/output.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <string>
#include <thread>
#include <vector>

namespace {

using wiola::engine::CommandResult;
using wiola::engine::PlaybackService;

TEST(PlaybackService, ReportsUnavailableCommandsWithoutLoadedMedia)
{
    PlaybackService service{
        [](wiola::audio::Source&) { return std::make_unique<wiola::testing::FakeOutput>(); }};

    EXPECT_EQ(service.play(), CommandResult::unavailable);
    EXPECT_EQ(service.pause(), CommandResult::unavailable);
    EXPECT_EQ(service.resume(), CommandResult::unavailable);
}

TEST(PlaybackService, SeparatesPlayPauseAndResume)
{
    PlaybackService service{
        [](wiola::audio::Source&) { return std::make_unique<wiola::testing::FakeOutput>(); },
        [](const std::filesystem::path&) -> wiola::codec::Opened {
            std::unique_ptr<wiola::codec::Decoder> decoder{
                std::make_unique<wiola::testing::FakeDecoder>()};
            return decoder;
        },
        [](const std::filesystem::path&) { return wiola::media::Metadata{}; }};

    service.open({"track.wav"});
    while (service.reading())
        std::this_thread::yield();
    service.catch_up();
    ASSERT_TRUE(service.loaded());

    EXPECT_EQ(service.play(), CommandResult::accepted);
    EXPECT_EQ(service.state(), wiola::engine::Playback::State::playing);
    EXPECT_EQ(service.pause(), CommandResult::accepted);
    EXPECT_EQ(service.state(), wiola::engine::Playback::State::paused);
    EXPECT_EQ(service.resume(), CommandResult::accepted);
    EXPECT_EQ(service.state(), wiola::engine::Playback::State::playing);
    service.stop();
}

TEST(PlaybackService, ExposesAudioControlsAsValues)
{
    PlaybackService service{
        [](wiola::audio::Source&) { return std::make_unique<wiola::testing::FakeOutput>(); }};

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
        [](const std::filesystem::path&) -> wiola::codec::Opened {
            std::unique_ptr<wiola::codec::Decoder> decoder{
                std::make_unique<wiola::testing::FakeDecoder>()};
            return decoder;
        },
        [](const std::filesystem::path&) { return wiola::media::Metadata{}; }};

    service.open({"headless.wav"});
    while (service.reading())
        std::this_thread::yield();

    const auto snapshot{service.pump()};
    EXPECT_TRUE(snapshot.loaded);
    EXPECT_EQ(snapshot.open_result, wiola::engine::OpenResult::opened);
    EXPECT_EQ(snapshot.track, std::filesystem::path{"headless.wav"});
    EXPECT_EQ(snapshot.queue_position, 0U);
    EXPECT_EQ(snapshot.total_time, service.total_time());
}

TEST(PlaybackService, ConnectsAnAcceptedOpenWithItsTypedFailure)
{
    PlaybackService service{
        [](wiola::audio::Source&) { return std::make_unique<wiola::testing::FakeOutput>(); },
        [](const std::filesystem::path&) {
            return wiola::codec::Opened{wiola::media::unexpected{
                wiola::media::Error{.category = wiola::media::ErrorCategory::unsupported,
                    .detail = "missing codec"}}};
        },
        [](const std::filesystem::path&) { return wiola::media::Metadata{}; }};

    const auto receipt{service.open({"unsupported.wav"})};
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
    ASSERT_FALSE(outcome->result);
    EXPECT_EQ(outcome->result.error().category, wiola::media::ErrorCategory::unsupported);
    EXPECT_EQ(outcome->result.error().detail, "missing codec");
    EXPECT_TRUE(service.take_outcomes().values.empty());
}

TEST(PlaybackService, SuppressesAnObsoleteOpenCompletion)
{
    wiola::testing::Gate opening;
    PlaybackService service{
        [](wiola::audio::Source&) { return std::make_unique<wiola::testing::FakeOutput>(); },
        [&opening](const std::filesystem::path& path) -> wiola::codec::Opened {
            if (path == "old.wav") {
                opening.wait();
                return wiola::media::unexpected{
                    wiola::media::Error{.category = wiola::media::ErrorCategory::unreadable,
                                        .detail = "old"}
                };
            }

            return wiola::codec::Opened{std::make_unique<wiola::testing::FakeDecoder>()};
        },
        [](const std::filesystem::path&) { return wiola::media::Metadata{}; }};

    const auto old{service.open({"old.wav"})};
    opening.wait_until_entered();
    const auto current{service.open({"current.wav"})};
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
        EXPECT_TRUE(outcome.result);
    }

    EXPECT_EQ(open_outcomes, 1U);
    EXPECT_EQ(service.open_result(), wiola::engine::OpenResult::opened);
}

TEST(PlaybackService, ReportsMetadataFailureWithoutSilencingPlayback)
{
    std::atomic<std::size_t> num_metadata_reads{0};
    PlaybackService service{
        [](wiola::audio::Source&) { return std::make_unique<wiola::testing::FakeOutput>(); },
        [](const std::filesystem::path&) -> wiola::codec::Opened {
            return wiola::codec::Opened{std::make_unique<wiola::testing::FakeDecoder>()};
        },
        [&num_metadata_reads](
            const std::filesystem::path&
        ) -> wiola::media::Result<wiola::media::Metadata> {
            ++num_metadata_reads;
            return wiola::media::unexpected{
                wiola::media::Error{.category = wiola::media::ErrorCategory::damaged,
                                    .detail = "bad tags"}
            };
        }};

    const auto receipt{service.open({"playable.wav"})};
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
    ASSERT_FALSE(metadata->result);
    EXPECT_EQ(metadata->result.error().category, wiola::media::ErrorCategory::damaged);
    EXPECT_EQ(metadata->result.error().detail, "bad tags");
    EXPECT_TRUE(service.loaded());
    EXPECT_EQ(service.open_result(), wiola::engine::OpenResult::opened);
}

TEST(PlaybackService, BoundsRetainedOutcomesAndReportsOverflow)
{
    std::atomic<std::size_t> num_metadata_reads{0};
    PlaybackService service{
        [](wiola::audio::Source&) { return std::make_unique<wiola::testing::FakeOutput>(); },
        [](const std::filesystem::path&) -> wiola::codec::Opened {
            return wiola::codec::Opened{std::make_unique<wiola::testing::FakeDecoder>()};
        },
        [&num_metadata_reads](const std::filesystem::path&) {
            ++num_metadata_reads;
            return wiola::media::Metadata{};
        }};
    std::vector<std::filesystem::path> tracks;

    for (std::size_t index{0}; index < 80; ++index)
        tracks.push_back("track-" + std::to_string(index) + ".wav");

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
