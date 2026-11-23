/**
 * @file
 * @brief A length of time, written the way a listener reads it.
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

#include "player_menu.hpp"
#include <QMenu>

namespace wiola::gui {

PlayerMenus build_player_menus(QMenuBar& bar, PlayerActions& actions, QObject& receiver,
    PlayerFileCommands commands)
{
    auto* file = bar.addMenu("&File");
    for (const auto id : {PlayerAction::open, PlayerAction::add, PlayerAction::clear})
        file->addAction(actions.action(id));
    file->addSeparator();
    QObject::connect(file->addAction("Open folder..."), &QAction::triggered, &receiver,
        std::move(commands.open_folder));
    file->addSeparator();
    QObject::connect(file->addAction("Import M3U8..."), &QAction::triggered, &receiver,
        std::move(commands.import_playlist));
    QObject::connect(file->addAction("Export M3U8..."), &QAction::triggered, &receiver,
        std::move(commands.export_playlist));
    auto* playback = bar.addMenu("&Playback");
    for (const auto id :
        {PlayerAction::previous, PlayerAction::play, PlayerAction::next, PlayerAction::stop})
        playback->addAction(actions.action(id));
    playback->addSeparator();
    for (const auto id : {PlayerAction::repeat, PlayerAction::shuffle})
        playback->addAction(actions.action(id));
    auto* view = bar.addMenu("&View");
    auto* tools = bar.addMenu("&Tools");
    tools->addAction(actions.action(PlayerAction::equalizer));
    return {view, tools};
}

} // namespace wiola::gui
