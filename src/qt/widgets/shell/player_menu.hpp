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

#pragma once
#include "player_actions.hpp"
#include <QMenuBar>
#include <functional>

namespace wiola::gui {

struct PlayerFileCommands {
    std::function<void()> open_folder, import_playlist, export_playlist, retry_save;
};

struct PlayerMenus {
    QMenu* view;
    QMenu* tools;
};

/// Menus borrow the same QAction objects as buttons and keyboard shortcuts.
[[nodiscard]] PlayerMenus build_player_menus(QMenuBar& bar, PlayerActions& actions,
    QObject& receiver, PlayerFileCommands commands);

} // namespace wiola::gui
