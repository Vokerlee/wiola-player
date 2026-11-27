/**
 * @file
 * @brief Validated widget geometry, typography and centralized appearance rendering.
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

#include <QFont>
#include <QString>
#include <span>

class QStyle;

namespace wiola::gui {

struct ThemeColors;

struct ThemeStyle {
    int corner_radius{8};
    int control_padding{6};
    int text_scale{100};
    bool operator==(const ThemeStyle&) const = default;
};

struct ThemeStyleMetric {
    const char* key;
    const char* label;
    const char* suffix;
    int ThemeStyle::* value;
    int minimum;
    int maximum;
};

[[nodiscard]] std::span<const ThemeStyleMetric> theme_style_metrics();
[[nodiscard]] bool valid_theme_style(const ThemeStyle& style);
[[nodiscard]] QStyle* make_theme_widget_style();
[[nodiscard]] QFont theme_font(const ThemeStyle& style);
[[nodiscard]] QString theme_stylesheet(const ThemeColors& colors, const ThemeStyle& style);

} // namespace wiola::gui
