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

#include <gui/player_presenter.hpp>

#include <fakes/decoder.hpp>
#include <fakes/output.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <thread>

namespace {

using wiola::engine::CommandResult;
using wiola::engine::Playback;
using wiola::gui::PlayerPresentation;
using Presenter = wiola::gui::PlayerPresenter;
using Repeat = wiola::engine::Playlist::Repeat;

class PlayerPresenter : public testing::Test {
protected:
    void SetUp() override
    {
        presenter = std::make_unique<Presenter>(
            [](wiola::audio::Source&) { return std::make_unique<wiola::testing::FakeOutput>(); },
            [](const std::filesystem::path&) -> wiola::codec::Opened {
                std::unique_ptr<wiola::codec::Decoder> decoder{
                    std::make_unique<wiola::testing::FakeDecoder>()};

                return decoder;
            },
            [](const std::filesystem::path&) { return wiola::media::Metadata{}; }
        );
    }

    static bool wait_for_track(Presenter& presenter, const std::filesystem::path& track)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};

        while (std::chrono::steady_clock::now() < deadline) {
            const PlayerPresentation state{presenter.pump()};

            if (state.playback.loaded && state.playback.track == track)
                return true;

            std::this_thread::yield();
        }

        return false;
    }

    std::unique_ptr<Presenter> presenter;
};

TEST_F(PlayerPresenter, ReportsUnavailablePlaybackWithoutMedia)
{
    const PlayerPresentation state{presenter->presentation()};

    EXPECT_FALSE(state.playback.loaded);
    EXPECT_FALSE(state.playback.playing);
    EXPECT_FALSE(state.playback.seekable);
    EXPECT_EQ(state.playback.state, Playback::State::idle);
    EXPECT_EQ(state.repeat, Repeat::none);
    EXPECT_FALSE(state.shuffled);

    EXPECT_EQ(presenter->toggle_playback(), CommandResult::unavailable);

    presenter->stop();
    presenter->seek_to(0.5);

    EXPECT_EQ(presenter->presentation().playback.state, Playback::State::idle);
}

TEST_F(PlayerPresenter, PumpsALoadIntoOneCoherentPresentation)
{
    const auto receipt{presenter->open({"track.wav"})};

    EXPECT_EQ(receipt.acceptance, CommandResult::accepted);
    ASSERT_TRUE(wait_for_track(*presenter, "track.wav"));

    const PlayerPresentation state{presenter->presentation()};

    EXPECT_TRUE(state.playback.loaded);
    EXPECT_FALSE(state.playback.playing);
    EXPECT_TRUE(state.playback.seekable);
    EXPECT_EQ(state.playback.open_result, wiola::engine::OpenResult::opened);
    EXPECT_EQ(state.playback.track, std::filesystem::path{"track.wav"});
    EXPECT_EQ(state.playback.queue_position, 0U);
    EXPECT_TRUE(state.playback.total_time.has_value());
    EXPECT_EQ(presenter->playlist().tracks().size(), 1U);
}

TEST_F(PlayerPresenter, TogglesBetweenPlayingPausedAndStopped)
{
    presenter->open({"track.wav"});
    ASSERT_TRUE(wait_for_track(*presenter, "track.wav"));

    EXPECT_EQ(presenter->toggle_playback(), CommandResult::accepted);
    EXPECT_EQ(presenter->presentation().playback.state, Playback::State::playing);

    EXPECT_EQ(presenter->toggle_playback(), CommandResult::accepted);
    EXPECT_EQ(presenter->presentation().playback.state, Playback::State::paused);

    EXPECT_EQ(presenter->toggle_playback(), CommandResult::accepted);
    EXPECT_EQ(presenter->presentation().playback.state, Playback::State::playing);

    presenter->stop();

    const PlayerPresentation stopped{presenter->presentation()};
    EXPECT_EQ(stopped.playback.state, Playback::State::stopped);
    EXPECT_FALSE(stopped.playback.playing);
    EXPECT_EQ(stopped.playback.time_played, wiola::units::Time{});
}

TEST_F(PlayerPresenter, RoutesQueueCommandsAndPresentsPlaybackPolicy)
{
    presenter->open({"first.wav", "second.wav", "third.wav"});
    ASSERT_TRUE(wait_for_track(*presenter, "first.wav"));

    ASSERT_TRUE(presenter->play_track(1));
    ASSERT_TRUE(wait_for_track(*presenter, "second.wav"));

    EXPECT_TRUE(presenter->next_track());
    ASSERT_TRUE(wait_for_track(*presenter, "third.wav"));

    EXPECT_TRUE(presenter->previous_track());
    ASSERT_TRUE(wait_for_track(*presenter, "second.wav"));

    presenter->cycle_repeat();
    EXPECT_EQ(presenter->presentation().repeat, Repeat::all);

    presenter->cycle_repeat();
    EXPECT_EQ(presenter->presentation().repeat, Repeat::track);

    presenter->cycle_repeat();
    EXPECT_EQ(presenter->presentation().repeat, Repeat::none);

    presenter->shuffle(true);

    const PlayerPresentation shuffled{presenter->presentation()};
    EXPECT_TRUE(shuffled.shuffled);
    EXPECT_EQ(shuffled.playback.queue_position, 1U);

    EXPECT_TRUE(presenter->remove(0));
    EXPECT_EQ(presenter->playlist().tracks().size(), 2U);
    EXPECT_EQ(presenter->presentation().playback.track, std::filesystem::path{"second.wav"});

    presenter->clear();

    EXPECT_TRUE(presenter->playlist().empty());
    EXPECT_EQ(presenter->presentation().playback.track, std::filesystem::path{"second.wav"});
    EXPECT_TRUE(presenter->presentation().playback.loaded);
}

TEST_F(PlayerPresenter, SeeksByFractionOfThePresentedDuration)
{
    presenter->open({"track.wav"});
    ASSERT_TRUE(wait_for_track(*presenter, "track.wav"));

    const auto total{presenter->presentation().playback.total_time};
    ASSERT_TRUE(total.has_value());

    presenter->seek_to(0.5);

    const wiola::units::Time elapsed{presenter->presentation().playback.time_played};
    EXPECT_NEAR(elapsed.get<wiola::units::Sec>(), total->get<wiola::units::Sec>() * 0.5, 0.001);
}

TEST_F(PlayerPresenter, RoutesVolumeAndEqualizerValues)
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
