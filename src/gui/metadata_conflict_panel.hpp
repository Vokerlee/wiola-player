/**
 * @file
 * @brief Explicit metadata conflict review and resolution controls.
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

#include <QLabel>
#include <QPushButton>
#include <QWidget>

#include <functional>

namespace wiola::gui {

enum class MetadataConflictAction {
    reload,
    keep,
    merge,
    review,
    overwrite,
};

class MetadataConflictPanel final : public QWidget {
public:
    using Observer = std::function<void(MetadataConflictAction)>;

    explicit MetadataConflictPanel(QWidget* parent = nullptr);

    void show_conflict(const QString& description);
    void hide_conflict();
    void on_action(Observer observer);

private:
    QLabel* description_{nullptr};
    QPushButton* reload_{nullptr};
    QPushButton* keep_{nullptr};
    QPushButton* merge_{nullptr};
    QPushButton* review_{nullptr};
    QPushButton* overwrite_{nullptr};
    Observer observer_;
};

} // namespace wiola::gui
