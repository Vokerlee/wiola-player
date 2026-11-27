/**
 * @file
 * @brief Editable theme copies with typed colors and contrast validation.
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

#include "theme.hpp"

#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>

#include <functional>
#include <map>
#include <vector>

namespace wiola::gui {

class ThemeEditor final : public QDialog {
public:
    using ApplyObserver = std::function<bool(const ThemeColors&)>;

    explicit ThemeEditor(const ThemeColors& initial, QWidget* parent = nullptr);

    void set_color(ThemeColorRole role, const QColor& color);
    void set_colors(const ThemeColors& colors);
    void set_style(const ThemeStyle& style);

    [[nodiscard]] ThemeStyle style() const noexcept { return style_; }

    [[nodiscard]] const ThemeColors& colors() const noexcept { return colors_; }

    void on_apply(ApplyObserver observer);

private:
    void update_color(ThemeColorRole role);
    void validate();
    void refresh_preview();

    ThemeColors colors_;
    ThemeStyle style_;
    std::vector<std::pair<int ThemeStyle::*, QSpinBox*>> metrics_;
    std::map<ThemeColorRole, QPushButton*> swatches_;
    QWidget* preview_{nullptr};
    QLabel* status_{nullptr};
    QPushButton* apply_{nullptr};
    ApplyObserver apply_observer_;
};

} // namespace wiola::gui
