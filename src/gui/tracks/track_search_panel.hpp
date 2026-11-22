/**
 * @file
 * @brief Multi-field track search with explicit reset controls.
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

#include <library/search.hpp>

#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QWidget>

#include <functional>

namespace wiola::gui {

class TrackSearchPanel final : public QWidget {
public:
    using Observer = std::function<void(library::SearchQuery)>;

    explicit TrackSearchPanel(QWidget* parent = nullptr);

    void show_results(std::size_t visible, std::size_t total);
    void on_changed(Observer observer);
    void reset();

private:
    [[nodiscard]] library::SearchQuery query() const;

    QLineEdit* title_{nullptr};
    QLineEdit* artist_{nullptr};
    QLineEdit* album_{nullptr};
    QPushButton* clear_title_{nullptr};
    QPushButton* clear_artist_{nullptr};
    QPushButton* clear_album_{nullptr};
    QPushButton* reset_{nullptr};
    QLabel* results_{nullptr};
    Observer observer_;
};

} // namespace wiola::gui
