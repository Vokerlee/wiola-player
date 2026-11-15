/**
 * @file
 * @brief Application command tests for PlaybackService.
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

#include <app/playback_service.hpp>

#include <fakes/decoder.hpp>
#include <fakes/output.hpp>

#include <gtest/gtest.h>

#include <thread>

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
        [](const std::filesystem::path&) { return wiola::codec::Tags{}; }};

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
        [](const std::filesystem::path&) { return wiola::codec::Tags{}; }};

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
