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

#include <core/version.hpp>
#include <desktop/library/queue_library_migration.hpp>
#include <desktop/player_application.hpp>
#include <gui/shell/main_window.hpp>

#include <QApplication>
#include <QDebug>
#include <QSettings>

#include <iostream>
#include <string_view>

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
    wiola::app::PlayerApplication player;

    QSettings settings{QSettings::IniFormat, QSettings::UserScope, "wiola-player", "settings"};
    const wiola::app::QueueLibraryMigration migration{settings};

    if (migration.migrate(settings) != wiola::gui::LibraryJsonError::none)
        qWarning("Could not migrate the queued tracks into the library");

    wiola::gui::MainWindow window{player.session()};

    player.attach(window);
    window.show();

    return QApplication::exec();
}
