/**
 * @file
 * @brief Application-owned playback state and widget-independent pumping.
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

#include "player_application.hpp"

#include <gui/main_window.hpp>
#include <gui/presentation_tokens.hpp>

namespace wiola::app {

PlayerApplication::PlayerApplication(QObject* parent)
    : QObject{parent}
{
    constexpr gui::PresentationTokens tokens;

    connect(&pump_timer_, &QTimer::timeout, this, [this] {
        session_.catch_up();
        emit pumped();
    });

    pump_timer_.start(tokens.player.refresh_interval);
    refresh_timer_.start(tokens.player.refresh_interval);
}

void PlayerApplication::attach(gui::MainWindow& window)
{
    connect(this, &PlayerApplication::pumped, &window, &gui::MainWindow::update_after_pump,
        Qt::UniqueConnection);
    connect(&refresh_timer_, &QTimer::timeout, &window, &gui::MainWindow::refresh,
        Qt::UniqueConnection);
}

} // namespace wiola::app
