/**
 * @file
 * @brief Program entry point.
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

#include <qt/application/library/queue_library_migration.hpp>
#include <qt/application/player_application.hpp>
#include <qt/widgets/shell/main_window.hpp>
#include <wiola/audio/miniaudio/device.hpp>
#include <wiola/media/codecs/ffmpeg/composition.hpp>
#include <wiola/version.hpp>

#include <QApplication>
#include <QDebug>
#include <QSettings>

#include <iostream>
#include <memory>
#include <string_view>

namespace {

wiola::app::PlayerRuntimeConfig desktop_runtime()
{
    wiola::app::PlayerRuntimeConfig config;
    config.provider = wiola::codec::ffmpeg::make_provider();
    config.output = [](wiola::audio::Source& source) {
        return std::make_unique<wiola::audio::Device>(source);
    };
    return config;
}

} // namespace

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument{argv[i]};

        if (argument == "-h" || argument == "--help") {
            std::cout << "Wiola media player\n"
                         "\n"
                         "Usage: wiola-player [OPTION]\n"
                         "\n"
                         "  -h, --help     Show this help and exit\n"
                         "  -v, --version  Show version and exit\n";
            return 0;
        }

        if (argument == "-v" || argument == "--version") {
            std::cout << "wiola-player " << wiola::version << '\n';
            return 0;
        }
    }

    const QApplication app{argc, argv};

    QSettings settings{QSettings::IniFormat, QSettings::UserScope, "wiola-player", "settings"};
    const wiola::app::QueueLibraryMigration migration{settings};

    if (migration.migrate(settings) != wiola::persistence::qt::LibraryStoreResult{})
        qWarning("Could not migrate the queued tracks into the library");

    wiola::app::PlayerApplication player{desktop_runtime()};
    wiola::gui::MainWindow window{player.playback(), player.library(), player.preferences()};

    player.observe(
        window, [&window](const auto& presentation) { window.update_after_pump(presentation); },
        [&window] { window.refresh(); }
    );
    window.show();

    const int result = QApplication::exec();
    if (!player.shutdown()) {
        qCritical("Could not flush player data during shutdown");
        return 1;
    }
    return result;
}
