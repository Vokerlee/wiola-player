/**
 * @file
 * @brief Include/exclude custom tag filters with removable chips.
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

#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QWidget>

#include <functional>

namespace wiola::gui {

struct TagFilterEdit {
    enum class Kind {
        add,
        remove,
    };
    Kind kind;
    library::TagFilter filter;
};

class TagFilterPanel final : public QWidget {
public:
    using Observer = std::function<void(TagFilterEdit)>;

    explicit TagFilterPanel(QWidget* parent = nullptr);

    void on_edit(Observer observer);
    void show_filters(std::vector<library::TagFilter> filters);

private:
    void add(library::TagFilterMode mode);
    void rebuild_chips();

    QLineEdit* key_{nullptr};
    QLineEdit* value_{nullptr};
    QPushButton* include_{nullptr};
    QPushButton* exclude_{nullptr};
    QHBoxLayout* chips_{nullptr};
    std::vector<library::TagFilter> shown_;
    std::vector<QPushButton*> chip_buttons_;
    Observer observer_;
};

} // namespace wiola::gui
