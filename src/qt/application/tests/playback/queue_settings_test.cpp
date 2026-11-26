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

#include <app/playback/queue_settings.hpp>
#include <qt/settings.hpp>

#include <media/local/resource.hpp>

#include <wiola/app/playback/playback_service.hpp>

#include <fakes/output.hpp>
#include <fixtures/wav.hpp>
#include <media/local/provider.hpp>

#include <QSettings>
#include <QString>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <thread>

namespace {

using Repeat = wiola::engine::RepeatMode;
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

    static bool available(const std::filesystem::path& path)
    {
        std::error_code error;
        return std::filesystem::exists(path, error) && !error;
    }

    inline static std::unique_ptr<QTemporaryDir> settings_directory;
    QSettings settings{QSettings::IniFormat, QSettings::UserScope, "wiola-test", "queue"};
    wiola::platform::qt::QtSettingsAdapter store{settings};
};

} // namespace

TEST_F(QueueSettings, RestoresNothingFromAnEmptySettingsFile)
{
    Presenter presenter{wiola::testing::passive_output,
        std::make_shared<wiola::codec::LocalMediaProvider>()};

    Control control{presenter, store, available};

    EXPECT_EQ(control.restore(), 0U);
    EXPECT_TRUE(presenter.playlist().empty());
}

TEST_F(QueueSettings, PutsBackTheQueueTheLastRunLeft)
{
    const std::filesystem::path first{wiola::testing::write_wav("wiola_queue.wav")};
    const std::filesystem::path next{wiola::testing::write_wav("wiola_queue_next.wav")};

    {
        Presenter presenter{wiola::testing::passive_output,
            std::make_shared<wiola::codec::LocalMediaProvider>()};

        Control control{presenter, store, available};

        presenter.add({wiola::media::local_resource(first), wiola::media::local_resource(next)});
        settle(presenter);
        control.save();
    }

    Presenter restored{wiola::testing::passive_output,
        std::make_shared<wiola::codec::LocalMediaProvider>()};

    Control control{restored, store, available};

    EXPECT_EQ(control.restore(), 0U);

    ASSERT_EQ(restored.playlist().tracks().size(), 2U);
    EXPECT_EQ(restored.playlist().tracks().front().resource(), wiola::media::local_resource(first));
    EXPECT_EQ(restored.playlist().tracks().back().resource(), wiola::media::local_resource(next));
}

/// It stands where it stood, without playing: putting a queue back is not pressing play.
TEST_F(QueueSettings, StandsWhereTheLastRunStood)
{
    const std::filesystem::path first{wiola::testing::write_wav("wiola_queue.wav")};
    const std::filesystem::path next{wiola::testing::write_wav("wiola_queue_next.wav")};

    {
        Presenter presenter{wiola::testing::passive_output,
            std::make_shared<wiola::codec::LocalMediaProvider>()};

        Control control{presenter, store, available};

        presenter.add({wiola::media::local_resource(first), wiola::media::local_resource(next)});
        settle(presenter);
        ASSERT_TRUE(presenter.play_track(1));
        settle(presenter);
        control.save();
    }

    Presenter restored{wiola::testing::passive_output,
        std::make_shared<wiola::codec::LocalMediaProvider>()};

    Control control{restored, store, available};

    ASSERT_EQ(control.restore(), 0U);
    settle(restored);

    EXPECT_EQ(restored.playlist().position(), 1U);
    EXPECT_EQ(wiola::app::present_playback(restored).playback.track,
        wiola::media::local_resource(next));
    EXPECT_FALSE(wiola::app::present_playback(restored).playback.playing);
}

TEST_F(QueueSettings, PutsBackHowItWasBeingPlayedThrough)
{
    {
        Presenter presenter{wiola::testing::passive_output,
            std::make_shared<wiola::codec::LocalMediaProvider>()};

        Control control{presenter, store, available};

        presenter.add({wiola::media::local_resource(wiola::testing::write_wav("wiola_queue.wav"))});
        settle(presenter);
        presenter.set_repeat(Repeat::track);
        presenter.shuffle(true);
        control.save();
    }

    Presenter restored{wiola::testing::passive_output,
        std::make_shared<wiola::codec::LocalMediaProvider>()};

    Control control{restored, store, available};

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
        Presenter presenter{wiola::testing::passive_output,
            std::make_shared<wiola::codec::LocalMediaProvider>()};

        Control control{presenter, store, available};

        presenter.add({wiola::media::local_resource(kept), wiola::media::local_resource(gone)});
        settle(presenter);
        control.save();
    }

    std::filesystem::remove(gone);

    Presenter restored{wiola::testing::passive_output,
        std::make_shared<wiola::codec::LocalMediaProvider>()};

    Control control{restored, store, available};

    EXPECT_EQ(control.restore(), 1U) << "the track that had gone was not counted";

    ASSERT_EQ(restored.playlist().tracks().size(), 1U);
    EXPECT_EQ(restored.playlist().tracks().front().resource(), wiola::media::local_resource(kept));
}

TEST_F(QueueSettings, RestoresTheSelectedOccurrenceOfADuplicatePath)
{
    const std::filesystem::path repeated{wiola::testing::write_wav("wiola_queue_repeat.wav")};
    const std::filesystem::path other{wiola::testing::write_wav("wiola_queue_other.wav")};

    {
        Presenter session{wiola::testing::passive_output,
            std::make_shared<wiola::codec::LocalMediaProvider>()};

        Control control{session, store, available};

        session.add({wiola::media::local_resource(repeated), wiola::media::local_resource(other),
            wiola::media::local_resource(repeated)});
        settle(session);

        ASSERT_TRUE(session.play_track(2));
        settle(session);
        ASSERT_EQ(session.playlist().position(), 2U);

        control.save();
    }

    Presenter restored{wiola::testing::passive_output,
        std::make_shared<wiola::codec::LocalMediaProvider>()};

    Control control{restored, store, available};

    ASSERT_EQ(control.restore(), 0U);
    settle(restored);

    const auto& tracks{restored.playlist().tracks()};

    ASSERT_EQ(tracks.size(), 3U);
    EXPECT_EQ(tracks[0].resource(), wiola::media::local_resource(repeated));
    EXPECT_EQ(tracks[1].resource(), wiola::media::local_resource(other));
    EXPECT_EQ(tracks[2].resource(), wiola::media::local_resource(repeated));
    EXPECT_NE(tracks[0].id(), tracks[2].id());

    EXPECT_EQ(restored.playlist().position(), 2U);
    EXPECT_EQ(control.restore_mapping().size(), 3);
    EXPECT_EQ(wiola::app::present_playback(restored).playback.track,
        wiola::media::local_resource(repeated));
    EXPECT_FALSE(wiola::app::present_playback(restored).playback.playing);
}

TEST_F(QueueSettings, LegacyPathOnlySelectionMigratesWithoutInventingAnOccurrence)
{
    const auto repeated = wiola::testing::write_wav("wiola_legacy_duplicate.wav");
    const auto other = wiola::testing::write_wav("wiola_legacy_other.wav");
    settings.setValue("queue",
        QStringList{QString::fromStdString(other.string()),
            QString::fromStdString(repeated.string()), QString::fromStdString(repeated.string())});
    settings.setValue("queue/path", QString::fromStdString(repeated.string()));
    Presenter playback{wiola::testing::passive_output,
        std::make_shared<wiola::codec::LocalMediaProvider>()};

    Control control{playback, store, available};
    ASSERT_EQ(control.restore(), 0);
    EXPECT_EQ(playback.playlist().position(), 1);
    EXPECT_TRUE(control.restore_mapping().empty());
}

TEST_F(QueueSettings, InvalidOccurrenceMetadataNeverSelectsADuplicateByFilename)
{
    const auto repeated = wiola::testing::write_wav("wiola_invalid_duplicate.wav");
    const auto other = wiola::testing::write_wav("wiola_invalid_other.wav");
    settings.setValue("queue",
        QStringList{QString::fromStdString(other.string()),
            QString::fromStdString(repeated.string()), QString::fromStdString(repeated.string())});
    settings.setValue("queue/path", QString::fromStdString(repeated.string()));
    settings.setValue("queue/entry_ids", QStringList{"11", "11", "13"});
    settings.setValue("queue/selected_id", "11");
    Presenter playback{wiola::testing::passive_output,
        std::make_shared<wiola::codec::LocalMediaProvider>()};

    Control control{playback, store, available};
    ASSERT_EQ(control.restore(), 0);
    EXPECT_TRUE(control.restore_mapping().empty());
    EXPECT_EQ(playback.playlist().position(), 0);
}

TEST_F(QueueSettings, FallsBackToTheFirstSurvivingEntryWhenTheSelectedFileIsGone)
{
    const std::filesystem::path first{wiola::testing::write_wav("wiola_queue_first.wav")};
    const std::filesystem::path selected{wiola::testing::write_wav("wiola_queue_selected.wav")};
    const std::filesystem::path last{wiola::testing::write_wav("wiola_queue_last.wav")};

    {
        Presenter session{wiola::testing::passive_output,
            std::make_shared<wiola::codec::LocalMediaProvider>()};

        Control control{session, store, available};

        session.add({wiola::media::local_resource(first), wiola::media::local_resource(selected),
            wiola::media::local_resource(last)});
        settle(session);

        ASSERT_TRUE(session.play_track(1));
        settle(session);
        control.save();
    }

    ASSERT_TRUE(std::filesystem::remove(selected));

    Presenter restored{wiola::testing::passive_output,
        std::make_shared<wiola::codec::LocalMediaProvider>()};

    Control control{restored, store, available};

    EXPECT_EQ(control.restore(), 1U);
    settle(restored);

    ASSERT_EQ(restored.playlist().tracks().size(), 2U);
    EXPECT_EQ(restored.playlist().tracks()[0].resource(), wiola::media::local_resource(first));
    EXPECT_EQ(restored.playlist().tracks()[1].resource(), wiola::media::local_resource(last));

    EXPECT_EQ(restored.playlist().position(), 0U);
    EXPECT_EQ(wiola::app::present_playback(restored).playback.track,
        wiola::media::local_resource(first));
    EXPECT_FALSE(wiola::app::present_playback(restored).playback.playing);
}

TEST_F(QueueSettings, KeepsTheSelectedEntryWhenTheRestoredQueueIsShuffled)
{
    const std::filesystem::path first{wiola::testing::write_wav("wiola_queue_first.wav")};
    const std::filesystem::path selected{wiola::testing::write_wav("wiola_queue_selected.wav")};
    const std::filesystem::path last{wiola::testing::write_wav("wiola_queue_last.wav")};

    {
        Presenter session{wiola::testing::passive_output,
            std::make_shared<wiola::codec::LocalMediaProvider>()};

        Control control{session, store, available};

        session.add({wiola::media::local_resource(first), wiola::media::local_resource(selected),
            wiola::media::local_resource(last)});
        settle(session);

        ASSERT_TRUE(session.play_track(1));
        settle(session);

        session.set_repeat(Repeat::all);
        session.shuffle(true);
        control.save();
    }

    Presenter restored{wiola::testing::passive_output,
        std::make_shared<wiola::codec::LocalMediaProvider>()};

    Control control{restored, store, available};

    ASSERT_EQ(control.restore(), 0U);
    settle(restored);

    EXPECT_EQ(wiola::app::present_playback(restored).repeat, Repeat::all);
    EXPECT_TRUE(wiola::app::present_playback(restored).shuffled);
    EXPECT_EQ(restored.playlist().position(), 1U);
    EXPECT_EQ(wiola::app::present_playback(restored).playback.track,
        wiola::media::local_resource(selected));
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
        Presenter session{wiola::testing::passive_output,
            std::make_shared<wiola::codec::LocalMediaProvider>()};

        Control control{session, store, available};

        session.add({wiola::media::local_resource(wiola::testing::write_wav("wiola_queue.wav"))});
        settle(session);
        control.save();
    }

    Presenter restored{wiola::testing::passive_output,
        std::make_shared<wiola::codec::LocalMediaProvider>()};

    Control control{restored, store, available};

    ASSERT_EQ(control.restore(), 0U);

    EXPECT_EQ(settings.value("volume").toInt(), 73);
    EXPECT_FALSE(settings.value("equalizer/enabled").toBool());

    EXPECT_EQ(other.value("queue").toString(), "another application's queue");
    EXPECT_EQ(other.value("volume").toInt(), 19);

    other.clear();
    other.sync();
}

TEST_F(QueueSettings, LegacySelectionIsRestoredAmongTheAppendedOccurrences)
{
    const auto existing = wiola::testing::write_wav("existing.wav");
    const auto first = wiola::testing::write_wav("first.wav");
    const auto selected = wiola::testing::write_wav("selected.wav");
    wiola::app::QueueSnapshot saved;
    saved.tracks = {first, selected};
    saved.selected = selected;
    wiola::platform::qt::QtSettingsAdapter{settings}.write_queue(saved);
    Presenter presenter{wiola::testing::passive_output,
        std::make_shared<wiola::codec::LocalMediaProvider>()};
    presenter.add({wiola::media::local_resource(existing)});
    Control control{presenter, store, available};

    EXPECT_EQ(control.restore(), 0U);
    EXPECT_EQ(presenter.playlist().position(), 2U);
    ASSERT_NE(presenter.playlist().current(), nullptr);
    EXPECT_EQ(presenter.playlist().current()->resource(), wiola::media::local_resource(selected));
}
