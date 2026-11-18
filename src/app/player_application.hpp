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

#pragma once

#include <engine/session.hpp>

#include <QObject>
#include <QTimer>

namespace wiola::gui {

class MainWindow;

}

namespace wiola::app {

/**
 * Owns playback for the whole application rather than for one arrangement of widgets.
 *
 * The pump keeps accepting decoder and metadata completions without a window. A window attached to
 * it observes those completions and refreshes itself; Qt disconnects that window when it is
 * destroyed, leaving the session and both timers alive.
 */
class PlayerApplication final : public QObject {
    Q_OBJECT

public:
    explicit PlayerApplication(QObject* parent = nullptr);

    [[nodiscard]] engine::Session& session() noexcept { return session_; }

    /// Connects `window` to application pumping and refreshing until the window is destroyed.
    void attach(gui::MainWindow& window);

signals:
    void pumped();

private:
    engine::Session session_;
    QTimer pump_timer_;
    QTimer refresh_timer_;
};

} // namespace wiola::app
