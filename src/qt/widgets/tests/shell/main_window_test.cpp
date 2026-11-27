/**
 * @file
 * @brief Offscreen behavioral tests for the main window's commands.
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

#include <fakes/decoder.hpp>
#include <fakes/output.hpp>
#include <fakes/provider.hpp>
#include <fixtures/wav.hpp>
#include <qt/application/library/library_session.hpp>
#include <qt/application/playback/playback_preferences.hpp>
#include <qt/widgets/controls/equalizer_panel.hpp>
#include <qt/widgets/controls/seek_bar.hpp>
#include <qt/widgets/documents/now_playing_document.hpp>
#include <qt/widgets/documents/saved_playlist_document.hpp>
#include <qt/widgets/documents/workspace_document.hpp>
#include <qt/widgets/metadata/track_inspector.hpp>
#include <qt/widgets/shell/document_host.hpp>
#include <qt/widgets/shell/main_window.hpp>
#include <qt/widgets/shell/saved_playlist_manager.hpp>
#include <qt/widgets/tests/support/widget_diagnostics.hpp>
#include <wiola/app/playback/volume_control.hpp>
#include <wiola/media/codecs/ffmpeg/composition.hpp>
#include <wiola/media/local/resource.hpp>

#include <QApplication>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QTemporaryDir>
#include <QThread>
#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <memory>
#include <vector>

namespace {

using wiola::gui::MainWindow;
using wiola::gui::SeekBar;

QPushButton* command(QWidget& window, const QString& accessible_name)
{
    const QList<QPushButton*> buttons{window.findChildren<QPushButton*>()};
    const auto found = std::ranges::find_if(buttons, [&accessible_name](const QPushButton* button) {
        return button->accessibleName() == accessible_name;
    });

    return found == buttons.end() ? nullptr : *found;
}

QAction* playback_command(MainWindow& window, const QString& name)
{
    for (auto* menu : window.menuBar()->findChildren<QMenu*>())
        if (menu->title() == "&Playback")
            for (auto* action : menu->actions())
                if (action->text() == name)
                    return action;
    return nullptr;
}

template<typename Widget>
Widget* with_text(QWidget& window, const QString& text)
{
    const QList<Widget*> widgets{window.findChildren<Widget*>()};
    const auto found = std::ranges::find_if(widgets,
        [&text](const Widget* widget) { return widget->text() == text; });

    return found == widgets.end() ? nullptr : *found;
}

QSlider* volume_slider(QWidget& window)
{
    const QList<QSlider*> sliders{window.findChildren<QSlider*>()};
    const auto found = std::ranges::find_if(sliders,
        [](const QSlider* slider) { return slider->orientation() == Qt::Horizontal; });

    return found == sliders.end() ? nullptr : *found;
}

QList<QSlider*> equalizer_sliders(QWidget& window)
{
    QList<QSlider*> bands;

    for (QSlider* slider : window.findChildren<QSlider*>()) {
        if (slider->orientation() == Qt::Vertical)
            bands.push_back(slider);
    }

    return bands;
}

template<typename Predicate>
bool pump_until(MainWindow& window, wiola::app::PlaybackService& playback, Predicate&& predicate)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};

    while (!predicate() && std::chrono::steady_clock::now() < deadline) {
        window.update_after_pump({.playback = playback.pump(),
            .repeat = playback.repeat(),
            .shuffled = playback.shuffled()});
        window.refresh();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }

    QCoreApplication::processEvents();

    return predicate();
}

bool loaded(MainWindow& window, wiola::app::PlaybackService& playback,
    const std::filesystem::path& track)
{
    const QPushButton* play = command(window, "Play");

    return play != nullptr && pump_until(window, playback, [&window, &track, play] {
        return play->isEnabled() &&
            window.windowTitle() == QString::fromStdString(track.filename().string());
    });
}

void drag_to(SeekBar& bar, double fraction)
{
    const QPointF point{bar.width() * fraction, bar.height() / 2.0};
    const QPointF global{bar.mapToGlobal(point.toPoint())};
    QMouseEvent press{QEvent::MouseButtonPress, point, global, Qt::LeftButton, Qt::LeftButton,
        Qt::NoModifier};
    QCoreApplication::sendEvent(&bar, &press);

    QMouseEvent release{QEvent::MouseButtonRelease, point, global, Qt::LeftButton, Qt::NoButton,
        Qt::NoModifier};
    QCoreApplication::sendEvent(&bar, &release);
}

class MainWindowTest : public ::testing::Test {
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
        QSettings settings = player_settings();
        settings.clear();

        for (const auto& name : {"layout.json", "layout.json.bak", "session.json",
                 "session.json.bak", "theme.json", "theme.json.bak"})
            QFile::remove(QFileInfo{settings.fileName()}.dir().filePath(name));

        settings.sync();
    }

    void TearDown() override
    {
        for (const std::filesystem::path& track : tracks)
            std::filesystem::remove(track);
    }

    static QSettings player_settings()
    {
        return QSettings{QSettings::IniFormat, QSettings::UserScope, "wiola-player", "settings"};
    }

    static QVariant stored(const char* key)
    {
        QSettings settings = player_settings();
        settings.sync();

        return settings.value(key);
    }

    std::filesystem::path track(const char* name)
    {
        tracks.push_back(wiola::testing::write_wav(name));

        return tracks.back();
    }

    inline static std::unique_ptr<QTemporaryDir> settings_directory;
    std::vector<std::filesystem::path> tracks;
};

TEST_F(MainWindowTest, StartsWithOnlyCommandsThatCanRunEnabled)
{
    wiola::app::PlaybackService window_playback{wiola::testing::passive_output,
        wiola::codec::ffmpeg::make_provider()};
    wiola::app::LibrarySession window_library{wiola::codec::ffmpeg::make_provider()};

    wiola::app::PlaybackPreferences window_preferences{window_playback};
    MainWindow window{window_playback, window_library, window_preferences};

    const QPushButton* open = command(window, "Open tracks");
    const QPushButton* previous = command(window, "Previous track");
    const QPushButton* play = command(window, "Play");
    const QPushButton* next = command(window, "Next track");
    const QAction* stop = playback_command(window, "Stop");
    const SeekBar* position = window.findChild<SeekBar*>();
    const QLabel* track_name = with_text<QLabel>(window, "no track");

    ASSERT_NE(open, nullptr);
    ASSERT_NE(previous, nullptr);
    ASSERT_NE(play, nullptr);
    ASSERT_NE(next, nullptr);
    ASSERT_NE(stop, nullptr);
    ASSERT_NE(position, nullptr);
    ASSERT_NE(track_name, nullptr);

    EXPECT_TRUE(open->isEnabled());
    EXPECT_FALSE(previous->isEnabled());
    EXPECT_FALSE(play->isEnabled());
    EXPECT_FALSE(next->isEnabled());
    EXPECT_FALSE(stop->isEnabled());
    EXPECT_FALSE(position->isEnabled());
}

TEST_F(MainWindowTest, LoadsTracksAndUsesTransportCommandsWithoutOpeningOutput)
{
    const std::filesystem::path first = track("wiola_window_first.wav");
    const std::filesystem::path second = track("wiola_window_second.wav");
    wiola::app::PlaybackService window_playback{wiola::testing::passive_output,
        wiola::codec::ffmpeg::make_provider()};
    wiola::app::LibrarySession window_library{wiola::codec::ffmpeg::make_provider()};

    wiola::app::PlaybackPreferences window_preferences{window_playback};
    MainWindow window{window_playback, window_library, window_preferences};

    QPushButton* previous = command(window, "Previous track");
    QPushButton* play = command(window, "Play");
    QPushButton* next = command(window, "Next track");
    QAction* stop = playback_command(window, "Stop");
    SeekBar* position = window.findChild<SeekBar*>();

    ASSERT_NE(previous, nullptr);
    ASSERT_NE(play, nullptr);
    ASSERT_NE(next, nullptr);
    ASSERT_NE(stop, nullptr);
    ASSERT_NE(position, nullptr);

    window.open({first, second});

    ASSERT_TRUE(loaded(window, window_playback, first));
    EXPECT_TRUE(previous->isEnabled());
    EXPECT_TRUE(play->isEnabled());
    EXPECT_TRUE(next->isEnabled());
    EXPECT_TRUE(stop->isEnabled());
    EXPECT_TRUE(position->isEnabled());
    EXPECT_EQ(play->accessibleName(), "Play");
    EXPECT_NE(with_text<QLabel>(window, "0:01"), nullptr);

    next->click();

    ASSERT_TRUE(pump_until(window, window_playback, [&window, &second] {
        return window.windowTitle() == QString::fromStdString(second.filename().string());
    }));
    EXPECT_EQ(play->accessibleName(), "Play");

    previous->click();

    ASSERT_TRUE(pump_until(window, window_playback, [&window, &first] {
        return window.windowTitle() == QString::fromStdString(first.filename().string());
    }));

    stop->trigger();
    QCoreApplication::processEvents();

    EXPECT_EQ(play->accessibleName(), "Play");
    EXPECT_TRUE(play->isEnabled());
    EXPECT_TRUE(stop->isEnabled());
}

TEST_F(MainWindowTest, SeeksThroughTheBarAndUpdatesTheDisplayedTime)
{
    const std::filesystem::path source = track("wiola_window_seek.wav");
    wiola::app::PlaybackService window_playback{wiola::testing::passive_output,
        wiola::codec::ffmpeg::make_provider()};
    wiola::app::LibrarySession window_library{wiola::codec::ffmpeg::make_provider()};

    wiola::app::PlaybackPreferences window_preferences{window_playback};
    MainWindow window{window_playback, window_library, window_preferences};
    SeekBar* position = window.findChild<SeekBar*>();

    ASSERT_NE(position, nullptr);

    window.open({source});

    ASSERT_TRUE(loaded(window, window_playback, source));

    QLabel* elapsed = with_text<QLabel>(window, "0:00");
    const QLabel* total = with_text<QLabel>(window, "0:01");

    ASSERT_NE(elapsed, nullptr);
    ASSERT_NE(total, nullptr);

    window.show();
    QCoreApplication::processEvents();

    constexpr double fraction{4.0 / 5.0};
    drag_to(*position, fraction);

    ASSERT_TRUE(pump_until(window, window_playback,
        [elapsed] { return elapsed->text() == "0:01"; }));
    EXPECT_NEAR(position->fraction(), fraction, 0.01);
}

TEST_F(MainWindowTest, RestoresVolumeAndBoostFromThePreviousWindow)
{
    {
        wiola::app::PlaybackService window_playback{wiola::testing::passive_output,
            wiola::codec::ffmpeg::make_provider()};
        wiola::app::LibrarySession window_library{wiola::codec::ffmpeg::make_provider()};

        wiola::app::PlaybackPreferences window_preferences{window_playback};
        MainWindow window{window_playback, window_library, window_preferences};
        QSlider* volume = volume_slider(window);
        QPushButton* boost = with_text<QPushButton>(window, "+");

        ASSERT_NE(volume, nullptr);
        ASSERT_NE(boost, nullptr);
        EXPECT_EQ(volume->value(), 100);
        EXPECT_EQ(volume->maximum(), 100);
        EXPECT_NE(with_text<QLabel>(window, "100%"), nullptr);

        volume->setValue(63);
        boost->click();
        volume->setValue(123);

        EXPECT_TRUE(boost->isChecked());
        EXPECT_EQ(volume->maximum(), 140);
        EXPECT_NE(with_text<QLabel>(window, "123%"), nullptr);
    }

    EXPECT_EQ(stored("volume").toInt(), 123);
    EXPECT_TRUE(stored("volume/boosted").toBool());

    wiola::app::PlaybackService restored_playback{wiola::testing::passive_output,
        wiola::codec::ffmpeg::make_provider()};
    wiola::app::LibrarySession restored_library{wiola::codec::ffmpeg::make_provider()};

    wiola::app::PlaybackPreferences restored_preferences{restored_playback};
    MainWindow restored{restored_playback, restored_library, restored_preferences};
    const QSlider* volume = volume_slider(restored);
    const QPushButton* boost = with_text<QPushButton>(restored, "+");

    ASSERT_NE(volume, nullptr);
    ASSERT_NE(boost, nullptr);
    EXPECT_EQ(volume->value(), 123);
    EXPECT_EQ(volume->maximum(), 140);
    EXPECT_TRUE(boost->isChecked());
    EXPECT_NE(with_text<QLabel>(restored, "123%"), nullptr);
}

TEST_F(MainWindowTest, RestoresEqualizerStateAndBandGainsFromThePreviousWindow)
{
    {
        wiola::app::PlaybackService window_playback{wiola::testing::passive_output,
            wiola::codec::ffmpeg::make_provider()};
        wiola::app::LibrarySession window_library{wiola::codec::ffmpeg::make_provider()};

        wiola::app::PlaybackPreferences window_preferences{window_playback};
        MainWindow window{window_playback, window_library, window_preferences};
        QPushButton* equalizer = with_text<QPushButton>(window, "EQ");

        ASSERT_NE(equalizer, nullptr);

        equalizer->click();
        QCoreApplication::processEvents();

        QCheckBox* enabled = with_text<QCheckBox>(window, "Equalizer");
        const QList<QSlider*> bands = equalizer_sliders(window);

        ASSERT_NE(enabled, nullptr);
        ASSERT_GT(bands.size(), 2);
        EXPECT_TRUE(enabled->isChecked());

        enabled->click();
        bands[2]->setValue(60);

        EXPECT_FALSE(enabled->isChecked());
        EXPECT_EQ(bands[2]->value(), 60);
    }

    EXPECT_FALSE(stored("equalizer/enabled").toBool());
    EXPECT_FLOAT_EQ(stored("equalizer/band/2").toFloat(), 6.0F);

    wiola::app::PlaybackService restored_playback{wiola::testing::passive_output,
        wiola::codec::ffmpeg::make_provider()};
    wiola::app::LibrarySession restored_library{wiola::codec::ffmpeg::make_provider()};

    wiola::app::PlaybackPreferences restored_preferences{restored_playback};
    MainWindow restored{restored_playback, restored_library, restored_preferences};
    QPushButton* equalizer = with_text<QPushButton>(restored, "EQ");

    ASSERT_NE(equalizer, nullptr);

    equalizer->click();
    QCoreApplication::processEvents();

    const QCheckBox* enabled = with_text<QCheckBox>(restored, "Equalizer");
    const QList<QSlider*> bands = equalizer_sliders(restored);

    ASSERT_NE(enabled, nullptr);
    ASSERT_GT(bands.size(), 2);
    EXPECT_FALSE(enabled->isChecked());
    EXPECT_EQ(bands[2]->value(), 60);
}

TEST_F(MainWindowTest, ExplainsOutputSetupFailureWithoutBlamingTheTrack)
{
    wiola::app::PlaybackService window_playback{
        [](wiola::audio::Source&) { return std::unique_ptr<wiola::audio::Output>{}; },
        std::make_shared<wiola::testing::FakeMediaProvider>(
            [](const wiola::media::ResourceReference&, const wiola::media::OperationContext&) {
                return wiola::media::DecoderOpenResult{
                    std::make_unique<wiola::testing::FakeDecoder>()};
            },
            [](const wiola::media::ResourceReference&, const wiola::media::OperationContext&) {
                return wiola::media::Metadata{};
            }
        )};
    wiola::app::LibrarySession window_library{wiola::codec::ffmpeg::make_provider()};

    wiola::app::PlaybackPreferences window_preferences{window_playback};
    MainWindow window{window_playback, window_library, window_preferences};

    window_playback.open({wiola::media::local_resource("valid-decoder")});
    ASSERT_TRUE(pump_until(window, window_playback, [&] {
        return window_playback.open_result() == wiola::engine::OpenResult::output_unavailable;
    }));
    EXPECT_NE(with_text<QLabel>(window, "audio output is unavailable"), nullptr);
    EXPECT_FALSE(window_playback.loaded());
}

} // namespace

int main(int argc, char** argv)
{
#ifdef WIOLA_TEST_TSAN
    // Qt 6.4's offscreen backing store trips TSan inside uninstrumented Qt worker code.
    qputenv("QT_QPA_PLATFORM", "minimal");
#else
    qputenv("QT_QPA_PLATFORM", "offscreen");
#endif

    ::testing::InitGoogleTest(&argc, argv);

    QApplication application{argc, argv};

    wiola::testing::retain_widget_screenshots();

    return RUN_ALL_TESTS();
}
