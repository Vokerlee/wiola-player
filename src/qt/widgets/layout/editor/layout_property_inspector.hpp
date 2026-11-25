/**
 * @file
 * @brief Interactive palette, hierarchy, and inspector for button layouts.
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
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QWidget>
#include <functional>
#include <qt/widgets/layout/layout_document.hpp>

namespace wiola::gui {

class LayoutPropertyInspector final : public QWidget {
public:
    explicit LayoutPropertyInspector(QWidget* parent = nullptr);

    void on_changed(std::function<void()> observer) { observer_ = std::move(observer); }

    void show_node(const LayoutNode* node);
    [[nodiscard]] std::optional<LayoutNode> configuration(const LayoutNode& original);

    [[nodiscard]] QString error() const { return error_->text(); }

private:
    QLineEdit *title_, *sizes_;
    QSpinBox *width_, *columns_;
    QComboBox *action_, *orientation_;
    QLabel* error_;
    bool updating_{};
    std::function<void()> observer_;
};

} // namespace wiola::gui
