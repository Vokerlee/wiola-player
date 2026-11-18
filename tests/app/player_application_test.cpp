/**
 * @file
 * @brief Application ownership tests for widget-independent playback pumping.
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

#include <app/player_application.hpp>
#include <gui/main_window.hpp>

#include <fixtures/wav.hpp>

#include <QApplication>
#include <QCoreApplication>
#include <QEventLoop>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <vector>

namespace {

template<typename Predicate>
bool pump_until(Predicate&& predicate)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};

    while (!predicate() && std::chrono::steady_clock::now() < deadline) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }

    QCoreApplication::processEvents();

    return predicate();
}

class PlayerApplication : public testing::Test {
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
        QSettings settings{QSettings::IniFormat, QSettings::UserScope, "wiola-player", "settings"};
        settings.clear();
        settings.sync();
    }

    void TearDown() override
    {
        for (const std::filesystem::path& track : tracks)
            std::filesystem::remove(track);
    }

    std::filesystem::path track(const char* name)
    {
        tracks.push_back(wiola::testing::write_wav(name));

        return tracks.back();
    }

    inline static std::unique_ptr<QTemporaryDir> settings_directory;
    std::vector<std::filesystem::path> tracks;
};

TEST_F(PlayerApplication, KeepsPumpingAfterTheWindowThatStartedALoadIsDestroyed)
{
    wiola::app::PlayerApplication player;
    const std::filesystem::path source = track("wiola_application_load.wav");

    {
        wiola::gui::MainWindow window{player.session()};
        player.attach(window);

        window.open({source});

        EXPECT_EQ(player.session().track(), std::filesystem::path{});
        EXPECT_FALSE(player.session().loaded());
    }

    ASSERT_TRUE(pump_until([&player] { return player.session().loaded(); }));

    EXPECT_EQ(player.session().track(), source);
    EXPECT_EQ(player.session().open_result(), wiola::engine::OpenResult::opened);
    EXPECT_FALSE(player.session().playing());
}

} // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    testing::InitGoogleTest(&argc, argv);
    QApplication application{argc, argv};

    return RUN_ALL_TESTS();
}
