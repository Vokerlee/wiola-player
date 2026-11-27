/**
 * @file
 * @brief Movable library and contextual track-details panels.
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

#include <QObject>
#include <QString>

#include <utility>
#include <vector>

class QDockWidget;
class QMainWindow;
class QMenu;
class QWidget;

namespace wiola::gui {

/// Owns desktop docking; custom central layouts may temporarily borrow the same live views.
class WindowPanels final : public QObject {
public:
    WindowPanels(QMainWindow& window, QWidget& library, QWidget& inspector,
        std::vector<QWidget*> library_views);
    void reconcile(QWidget& central);
    [[nodiscard]] bool contains(const QWidget& widget) const;
    void add_menu_actions(QMenu& menu);
    void show_inspector();
    void reset();

private:
    QMainWindow& window_;
    QWidget& library_;
    QWidget& inspector_;
    std::vector<std::pair<QWidget*, QString>> library_views_;
    QString library_name_;
    QString inspector_name_;
    QDockWidget* library_dock_;
    QDockWidget* inspector_dock_;
    bool library_embedded_{};
    bool inspector_embedded_{};
    bool library_open_{true};
    bool inspector_open_{};
};

} // namespace wiola::gui
