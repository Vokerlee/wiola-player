/**
 * @file
 * @brief Command and presentation tests for the persistent player presenter.
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

#include <fakes/resource.hpp>
#include <wiola/app/playback/playback_service.hpp>

#include <fakes/provider.hpp>

#include <fakes/decoder.hpp>
#include <fakes/output.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <thread>

namespace {

using wiola::app::PlayerPresentation;
using wiola::engine::CommandResult;
using wiola::engine::PlaybackState;
using Presenter = wiola::app::PlaybackService;
using Repeat = wiola::engine::RepeatMode;

class PlaybackPresentation : public testing::Test {
protected:
    void SetUp() override
    {
        presenter = std::make_unique<Presenter>(
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
            )
        );
    }

    static bool wait_for_track(Presenter& presenter, const wiola::media::ResourceReference& track)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};

        while (std::chrono::steady_clock::now() < deadline) {
            static_cast<void>(presenter.pump());
            const PlayerPresentation state{wiola::app::present_playback(presenter)};

            if (state.playback.loaded && state.playback.track == track)
                return true;

            std::this_thread::yield();
        }

        return false;
    }

    std::unique_ptr<Presenter> presenter;
};

TEST_F(PlaybackPresentation, ReportsUnavailablePlaybackWithoutMedia)
{
    const PlayerPresentation state{wiola::app::present_playback(*presenter)};

    EXPECT_FALSE(state.playback.loaded);
    EXPECT_FALSE(state.playback.playing);
    EXPECT_FALSE(state.playback.seekable);
    EXPECT_EQ(state.playback.state, PlaybackState::idle);
    EXPECT_EQ(state.repeat, Repeat::none);
    EXPECT_FALSE(state.shuffled);

    EXPECT_EQ(presenter->toggle_playback(), CommandResult::unavailable);

    presenter->stop();
    presenter->seek_to(0.5);

    EXPECT_EQ(wiola::app::present_playback(*presenter).playback.state, PlaybackState::idle);
}

TEST_F(PlaybackPresentation, PumpsALoadIntoOneCoherentPresentation)
{
    const auto receipt{presenter->open({wiola::testing::resource("track.wav", "Track")})};

    EXPECT_EQ(receipt.acceptance, CommandResult::accepted);
    ASSERT_TRUE(wait_for_track(*presenter, wiola::testing::resource("track.wav", "Track")));

    const PlayerPresentation state{wiola::app::present_playback(*presenter)};

    EXPECT_TRUE(state.playback.loaded);
    EXPECT_FALSE(state.playback.playing);
    EXPECT_TRUE(state.playback.seekable);
    EXPECT_EQ(state.playback.open_result, wiola::engine::OpenResult::opened);
    EXPECT_EQ(state.playback.track, wiola::testing::resource("track.wav", "Track"));
    EXPECT_EQ(state.playback.queue_position, 0U);
    EXPECT_TRUE(state.playback.total_time.has_value());
    EXPECT_EQ(presenter->playlist().tracks().size(), 1U);
}

TEST_F(PlaybackPresentation, TogglesBetweenPlayingPausedAndStopped)
{
    presenter->open({wiola::testing::resource("track.wav", "Track")});
    ASSERT_TRUE(wait_for_track(*presenter, wiola::testing::resource("track.wav", "Track")));

    EXPECT_EQ(presenter->toggle_playback(), CommandResult::accepted);
    EXPECT_EQ(wiola::app::present_playback(*presenter).playback.state, PlaybackState::playing);

    EXPECT_EQ(presenter->toggle_playback(), CommandResult::accepted);
    EXPECT_EQ(wiola::app::present_playback(*presenter).playback.state, PlaybackState::paused);

    EXPECT_EQ(presenter->toggle_playback(), CommandResult::accepted);
    EXPECT_EQ(wiola::app::present_playback(*presenter).playback.state, PlaybackState::playing);

    presenter->stop();

    const PlayerPresentation stopped{wiola::app::present_playback(*presenter)};
    EXPECT_EQ(stopped.playback.state, PlaybackState::stopped);
    EXPECT_FALSE(stopped.playback.playing);
    EXPECT_EQ(stopped.playback.time_played, wiola::units::Time{});
}

TEST_F(PlaybackPresentation, RoutesQueueCommandsAndPresentsPlaybackPolicy)
{
    presenter->open({wiola::testing::resource("first.wav", "Track"),
        wiola::testing::resource("second.wav", "Track"),
        wiola::testing::resource("third.wav", "Track")});
    ASSERT_TRUE(wait_for_track(*presenter, wiola::testing::resource("first.wav", "Track")));

    ASSERT_TRUE(presenter->play_track(1));
    ASSERT_TRUE(wait_for_track(*presenter, wiola::testing::resource("second.wav", "Track")));

    EXPECT_TRUE(presenter->next_track());
    ASSERT_TRUE(wait_for_track(*presenter, wiola::testing::resource("third.wav", "Track")));

    EXPECT_TRUE(presenter->previous_track());
    ASSERT_TRUE(wait_for_track(*presenter, wiola::testing::resource("second.wav", "Track")));

    presenter->cycle_repeat();
    EXPECT_EQ(wiola::app::present_playback(*presenter).repeat, Repeat::all);

    presenter->cycle_repeat();
    EXPECT_EQ(wiola::app::present_playback(*presenter).repeat, Repeat::track);

    presenter->cycle_repeat();
    EXPECT_EQ(wiola::app::present_playback(*presenter).repeat, Repeat::none);

    presenter->shuffle(true);

    const PlayerPresentation shuffled{wiola::app::present_playback(*presenter)};
    EXPECT_TRUE(shuffled.shuffled);
    EXPECT_EQ(shuffled.playback.queue_position, 1U);

    EXPECT_TRUE(presenter->remove(0));
    EXPECT_EQ(presenter->playlist().tracks().size(), 2U);
    EXPECT_EQ(wiola::app::present_playback(*presenter).playback.track,
        wiola::testing::resource("second.wav", "Track"));

    presenter->clear();

    EXPECT_TRUE(presenter->playlist().empty());
    EXPECT_EQ(wiola::app::present_playback(*presenter).playback.track,
        wiola::testing::resource("second.wav", "Track"));
    EXPECT_TRUE(wiola::app::present_playback(*presenter).playback.loaded);
}

TEST_F(PlaybackPresentation, SeeksByFractionOfThePresentedDuration)
{
    presenter->open({wiola::testing::resource("track.wav", "Track")});
    ASSERT_TRUE(wait_for_track(*presenter, wiola::testing::resource("track.wav", "Track")));

    const auto total{wiola::app::present_playback(*presenter).playback.total_time};
    ASSERT_TRUE(total.has_value());

    presenter->seek_to(0.5);

    const wiola::units::Time elapsed{wiola::app::present_playback(*presenter).playback.time_played};
    EXPECT_NEAR(elapsed.get<wiola::units::Sec>(), total->get<wiola::units::Sec>() * 0.5, 0.001);
}

TEST_F(PlaybackPresentation, RoutesVolumeAndEqualizerValues)
{
    presenter->set_volume(0.25F);
    presenter->set_equalizer_enabled(false);
    ASSERT_GT(presenter->num_equalizer_bands(), 2U);
    presenter->set_equalizer_band(2, 4.0F);

    EXPECT_FLOAT_EQ(presenter->volume(), 0.25F);
    EXPECT_FALSE(presenter->equalizer_enabled());
    EXPECT_FLOAT_EQ(presenter->equalizer_band(2), 4.0F);
}

} // namespace
