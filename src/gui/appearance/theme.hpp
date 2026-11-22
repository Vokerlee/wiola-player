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
#include <QIcon>
#include <QPalette>
#include <QPixmap>
#include <QStyle>

namespace wiola::gui {

enum class Theme {
    light,
    dark,
};

enum class ThemeColorRole {
    window,
    surface,
    text,
    muted_text,
    disabled_text,
    accent,
    accent_text,
    focus,
    error,
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
[[nodiscard]] QColor& theme_color(ThemeColors& colors, ThemeColorRole role);
[[nodiscard]] const QColor& theme_color(const ThemeColors& colors, ThemeColorRole role);
[[nodiscard]] const char* theme_color_key(ThemeColorRole role);
[[nodiscard]] double contrast_ratio(const QColor& first, const QColor& second);
[[nodiscard]] QString theme_contrast_error(const ThemeColors& colors);
[[nodiscard]] QPalette theme_palette(const ThemeColors& colors);
[[nodiscard]] QIcon themed_standard_icon(const QWidget& styled, QStyle::StandardPixmap icon);
[[nodiscard]] QPixmap themed_artwork_placeholder(const QWidget& styled, int size);
void apply_theme(Theme theme);
void apply_theme(const ThemeColors& colors);

} // namespace wiola::gui
