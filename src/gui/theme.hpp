/**
 * @file
 * @brief Semantic native light and dark application themes.
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

#include <QColor>
#include <QPalette>

namespace wiola::gui {

enum class Theme {
    light,
    dark,
};

struct ThemeColors {
    QColor window;
    QColor surface;
    QColor text;
    QColor muted_text;
    QColor disabled_text;
    QColor accent;
    QColor accent_text;
    QColor focus;
    QColor error;
};

[[nodiscard]] ThemeColors theme_colors(Theme theme);
[[nodiscard]] QPalette theme_palette(const ThemeColors& colors);
void apply_theme(Theme theme);

} // namespace wiola::gui
