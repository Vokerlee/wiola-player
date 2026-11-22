/**
 * @file
 * @brief Unit tests for the queue a run leaves behind.
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

#include <desktop/playback/queue_settings.hpp>

#include <app/playback/playback_service.hpp>

#include <fakes/output.hpp>
#include <fixtures/wav.hpp>

#include <QSettings>
#include <QString>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <thread>

namespace {

using Repeat = wiola::engine::Playlist::Repeat;
using Control = wiola::app::QueueSettings;
using Presenter = wiola::app::PlaybackService;

/// A settings file of its own, so a test neither reads nor leaves anything of the user's, and a
/// presenter that plays nowhere real.
class QueueSettings : public testing::Test {
protected:
    static void SetUpTestSuite()
    {
        settings_directory = std::make_unique<QTemporaryDir>();
        ASSERT_TRUE(settings_directory->isValid());

        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings_directory->path());
    }

    static void TearDownTestSuite() { settings_directory.reset(); }

    void SetUp() override
    {
        settings.clear();
        settings.sync();
    }

    /// Turns the presenter over until it has caught up with what was asked of it.
    static void settle(Presenter& presenter)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};

        while (presenter.reading() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::yield();

        static_cast<void>(presenter.pump());
    }

    static wiola::engine::OutputFactory nowhere()
    {
        return [](wiola::audio::Source& /*source*/) {
            return std::make_unique<wiola::testing::FakeOutput>();
        };
    }

    inline static std::unique_ptr<QTemporaryDir> settings_directory;
    QSettings settings{QSettings::IniFormat, QSettings::UserScope, "wiola-test", "queue"};
};

} // namespace

TEST_F(QueueSettings, RestoresNothingFromAnEmptySettingsFile)
{
    wiola::engine::Session presenter_engine{nowhere()};
    Presenter presenter{presenter_engine};
    Control control{presenter, settings};

    EXPECT_EQ(control.restore(), 0U);
    EXPECT_TRUE(presenter.playlist().empty());
}

TEST_F(QueueSettings, PutsBackTheQueueTheLastRunLeft)
{
    const std::filesystem::path first{wiola::testing::write_wav("wiola_queue.wav")};
    const std::filesystem::path next{wiola::testing::write_wav("wiola_queue_next.wav")};

    {
        wiola::engine::Session presenter_engine{nowhere()};
        Presenter presenter{presenter_engine};
        Control control{presenter, settings};

        presenter.add({first, next});
        settle(presenter);
        control.save();
    }

    wiola::engine::Session restored_engine{nowhere()};

    Presenter restored{restored_engine};
    Control control{restored, settings};

    EXPECT_EQ(control.restore(), 0U);

    ASSERT_EQ(restored.playlist().tracks().size(), 2U);
    EXPECT_EQ(restored.playlist().tracks().front().path(), first);
    EXPECT_EQ(restored.playlist().tracks().back().path(), next);
}

/// It stands where it stood, without playing: putting a queue back is not pressing play.
TEST_F(QueueSettings, StandsWhereTheLastRunStood)
{
    const std::filesystem::path first{wiola::testing::write_wav("wiola_queue.wav")};
    const std::filesystem::path next{wiola::testing::write_wav("wiola_queue_next.wav")};

    {
        wiola::engine::Session presenter_engine{nowhere()};
        Presenter presenter{presenter_engine};
        Control control{presenter, settings};

        presenter.add({first, next});
        settle(presenter);
        ASSERT_TRUE(presenter.play_track(1));
        settle(presenter);
        control.save();
    }

    wiola::engine::Session restored_engine{nowhere()};

    Presenter restored{restored_engine};
    Control control{restored, settings};

    ASSERT_EQ(control.restore(), 0U);
    settle(restored);

    EXPECT_EQ(restored.playlist().position(), 1U);
    EXPECT_EQ(wiola::app::present_playback(restored).playback.track, next);
    EXPECT_FALSE(wiola::app::present_playback(restored).playback.playing);
}

TEST_F(QueueSettings, PutsBackHowItWasBeingPlayedThrough)
{
    {
        wiola::engine::Session presenter_engine{nowhere()};
        Presenter presenter{presenter_engine};
        Control control{presenter, settings};

        presenter.add({wiola::testing::write_wav("wiola_queue.wav")});
        settle(presenter);
        presenter.set_repeat(Repeat::track);
        presenter.shuffle(true);
        control.save();
    }

    wiola::engine::Session restored_engine{nowhere()};

    Presenter restored{restored_engine};
    Control control{restored, settings};

    ASSERT_EQ(control.restore(), 0U);

    EXPECT_EQ(wiola::app::present_playback(restored).repeat, Repeat::track);
    EXPECT_TRUE(wiola::app::present_playback(restored).shuffled);
}

/// A queue that cannot be played is worse than a shorter one.
TEST_F(QueueSettings, DropsTracksThatHaveGoneSince)
{
    const std::filesystem::path kept{wiola::testing::write_wav("wiola_queue.wav")};
    const std::filesystem::path gone{wiola::testing::write_wav("wiola_queue_gone.wav")};

    {
        wiola::engine::Session presenter_engine{nowhere()};
        Presenter presenter{presenter_engine};
        Control control{presenter, settings};

        presenter.add({kept, gone});
        settle(presenter);
        control.save();
    }

    std::filesystem::remove(gone);

    wiola::engine::Session restored_engine{nowhere()};

    Presenter restored{restored_engine};
    Control control{restored, settings};

    EXPECT_EQ(control.restore(), 1U) << "the track that had gone was not counted";

    ASSERT_EQ(restored.playlist().tracks().size(), 1U);
    EXPECT_EQ(restored.playlist().tracks().front().path(), kept);
}

TEST_F(QueueSettings, RestoresTheFirstOccurrenceOfASelectedDuplicatePath)
{
    const std::filesystem::path repeated{wiola::testing::write_wav("wiola_queue_repeat.wav")};
    const std::filesystem::path other{wiola::testing::write_wav("wiola_queue_other.wav")};

    {
        wiola::engine::Session session_engine{nowhere()};
        Presenter session{session_engine};
        Control control{session, settings};

        session.add({repeated, other, repeated});
        settle(session);

        ASSERT_TRUE(session.play_track(2));
        settle(session);
        ASSERT_EQ(session.playlist().position(), 2U);

        control.save();
    }

    wiola::engine::Session restored_engine{nowhere()};

    Presenter restored{restored_engine};
    Control control{restored, settings};

    ASSERT_EQ(control.restore(), 0U);
    settle(restored);

    const auto& tracks{restored.playlist().tracks()};

    ASSERT_EQ(tracks.size(), 3U);
    EXPECT_EQ(tracks[0].path(), repeated);
    EXPECT_EQ(tracks[1].path(), other);
    EXPECT_EQ(tracks[2].path(), repeated);
    EXPECT_NE(tracks[0].id(), tracks[2].id());

    EXPECT_EQ(restored.playlist().position(), 0U);
    EXPECT_EQ(wiola::app::present_playback(restored).playback.track, repeated);
    EXPECT_FALSE(wiola::app::present_playback(restored).playback.playing);
}

TEST_F(QueueSettings, FallsBackToTheFirstSurvivingEntryWhenTheSelectedFileIsGone)
{
    const std::filesystem::path first{wiola::testing::write_wav("wiola_queue_first.wav")};
    const std::filesystem::path selected{wiola::testing::write_wav("wiola_queue_selected.wav")};
    const std::filesystem::path last{wiola::testing::write_wav("wiola_queue_last.wav")};

    {
        wiola::engine::Session session_engine{nowhere()};
        Presenter session{session_engine};
        Control control{session, settings};

        session.add({first, selected, last});
        settle(session);

        ASSERT_TRUE(session.play_track(1));
        settle(session);
        control.save();
    }

    ASSERT_TRUE(std::filesystem::remove(selected));

    wiola::engine::Session restored_engine{nowhere()};

    Presenter restored{restored_engine};
    Control control{restored, settings};

    EXPECT_EQ(control.restore(), 1U);
    settle(restored);

    ASSERT_EQ(restored.playlist().tracks().size(), 2U);
    EXPECT_EQ(restored.playlist().tracks()[0].path(), first);
    EXPECT_EQ(restored.playlist().tracks()[1].path(), last);

    EXPECT_EQ(restored.playlist().position(), 0U);
    EXPECT_EQ(wiola::app::present_playback(restored).playback.track, first);
    EXPECT_FALSE(wiola::app::present_playback(restored).playback.playing);
}

TEST_F(QueueSettings, KeepsTheSelectedEntryWhenTheRestoredQueueIsShuffled)
{
    const std::filesystem::path first{wiola::testing::write_wav("wiola_queue_first.wav")};
    const std::filesystem::path selected{wiola::testing::write_wav("wiola_queue_selected.wav")};
    const std::filesystem::path last{wiola::testing::write_wav("wiola_queue_last.wav")};

    {
        wiola::engine::Session session_engine{nowhere()};
        Presenter session{session_engine};
        Control control{session, settings};

        session.add({first, selected, last});
        settle(session);

        ASSERT_TRUE(session.play_track(1));
        settle(session);

        session.set_repeat(Repeat::all);
        session.shuffle(true);
        control.save();
    }

    wiola::engine::Session restored_engine{nowhere()};

    Presenter restored{restored_engine};
    Control control{restored, settings};

    ASSERT_EQ(control.restore(), 0U);
    settle(restored);

    EXPECT_EQ(wiola::app::present_playback(restored).repeat, Repeat::all);
    EXPECT_TRUE(wiola::app::present_playback(restored).shuffled);
    EXPECT_EQ(restored.playlist().position(), 1U);
    EXPECT_EQ(wiola::app::present_playback(restored).playback.track, selected);
    EXPECT_FALSE(wiola::app::present_playback(restored).playback.playing);
}

TEST_F(QueueSettings, LeavesUnrelatedSettingsAndAnotherApplicationsFileAlone)
{
    QSettings other{QSettings::IniFormat, QSettings::UserScope, "wiola-test", "another-queue"};
    other.clear();
    other.setValue("queue", "another application's queue");
    other.setValue("volume", 19);

    settings.setValue("volume", 73);
    settings.setValue("equalizer/enabled", false);

    {
        wiola::engine::Session session_engine{nowhere()};
        Presenter session{session_engine};
        Control control{session, settings};

        session.add({wiola::testing::write_wav("wiola_queue.wav")});
        settle(session);
        control.save();
    }

    wiola::engine::Session restored_engine{nowhere()};

    Presenter restored{restored_engine};
    Control control{restored, settings};

    ASSERT_EQ(control.restore(), 0U);

    EXPECT_EQ(settings.value("volume").toInt(), 73);
    EXPECT_FALSE(settings.value("equalizer/enabled").toBool());

    EXPECT_EQ(other.value("queue").toString(), "another application's queue");
    EXPECT_EQ(other.value("volume").toInt(), 19);

    other.clear();
    other.sync();
}
