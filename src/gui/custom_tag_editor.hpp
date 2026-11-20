/**
 * @file
 * @brief Accessible editor for shared custom tag values.
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

#include <library/records.hpp>

#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QWidget>

#include <functional>

namespace wiola::gui {

class CustomTagEditor final : public QWidget {
public:
    using AddObserver = std::function<void(library::MediaId, std::string, std::string)>;
    using EditObserver =
        std::function<void(library::MediaId, library::TagId, std::string, std::string)>;
    using RemoveObserver = std::function<void(library::MediaId, library::TagId)>;

    explicit CustomTagEditor(QWidget* parent = nullptr);

    void show_track(const library::TrackRecord* track);
    void on_add(AddObserver observer);
    void on_edit(EditObserver observer);
    void on_remove(RemoveObserver observer);

private:
    [[nodiscard]] library::TagId selected_tag() const;
    void show_selection();
    void update_actions();

    library::MediaId media_;
    std::uint64_t revision_{0};
    bool initialized_{false};
    QLabel* explanation_{nullptr};
    QListWidget* values_{nullptr};
    QLineEdit* key_{nullptr};
    QLineEdit* value_{nullptr};
    QPushButton* add_{nullptr};
    QPushButton* edit_{nullptr};
    QPushButton* remove_{nullptr};
    AddObserver add_observer_;
    EditObserver edit_observer_;
    RemoveObserver remove_observer_;
};

} // namespace wiola::gui
