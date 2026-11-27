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

#include "theme.hpp"

#include <QApplication>
#include <QPainter>
#include <QPainterPath>
#include <QWidget>

#include <algorithm>
#include <array>
#include <cmath>

namespace wiola::gui {

namespace {

QColor mix(const QColor& first, const QColor& second, int percentage)
{
    const int inverse = 100 - percentage;
    return QColor{
        (first.red() * percentage + second.red() * inverse) / 100,
        (first.green() * percentage + second.green() * inverse) / 100,
        (first.blue() * percentage + second.blue() * inverse) / 100,
    };
}

void set_text_roles(QPalette& palette, QPalette::ColorGroup group, const QColor& text)
{
    for (const QPalette::ColorRole role : {QPalette::WindowText, QPalette::Text,
             QPalette::ButtonText, QPalette::ToolTipText, QPalette::BrightText})
        palette.setColor(group, role, text);
}

} // namespace

ThemeColors theme_colors(Theme theme)
{
    if (theme == Theme::midnight)
        return {QColor{"#10121c"}, QColor{"#191d2b"}, QColor{"#edf0ff"}, QColor{"#afb7d2"},
            QColor{"#7f8aaa"}, QColor{"#b8a3ff"}, QColor{"#1b1430"}, QColor{"#d1c2ff"},
            QColor{"#ff9f9f"}};

    if (theme == Theme::studio)
        return {QColor{"#161b1b"}, QColor{"#202727"}, QColor{"#eff6f3"}, QColor{"#b2c4bc"},
            QColor{"#7e958a"}, QColor{"#6fe0ad"}, QColor{"#10271d"}, QColor{"#94efc3"},
            QColor{"#ffa5a5"}};

    if (theme == Theme::dark) {
        return {
            .window = QColor{"#202124"},
            .surface = QColor{"#292a2d"},
            .text = QColor{"#f1f3f4"},
            .muted_text = QColor{"#bdc1c6"},
            .disabled_text = QColor{"#80868b"},
            .accent = QColor{"#8ab4f8"},
            .accent_text = QColor{"#17233b"},
            .focus = QColor{"#a8c7fa"},
            .error = QColor{"#f28b82"},
        };
    }

    return {
        .window = QColor{"#f7f7f8"},
        .surface = QColor{"#ffffff"},
        .text = QColor{"#202124"},
        .muted_text = QColor{"#5f6368"},
        .disabled_text = QColor{"#80868b"},
        .accent = QColor{"#1a73e8"},
        .accent_text = QColor{"#ffffff"},
        .focus = QColor{"#185abc"},
        .error = QColor{"#b3261e"},
    };
}

std::span<const ThemePreset> theme_presets()
{
    static constexpr std::array presets{
        ThemePreset{ThemeChoice::light,    Theme::light,    "light",    "Light theme", {}          },
        ThemePreset{ThemeChoice::dark,     Theme::dark,     "dark",     "Dark theme",  {}          },
        ThemePreset{ThemeChoice::midnight, Theme::midnight, "midnight", "Midnight",    {12, 8, 105}},
        ThemePreset{ThemeChoice::studio,   Theme::studio,   "studio",   "Studio",      {3, 4, 100} },
    };
    return presets;
}

QColor& theme_color(ThemeColors& colors, ThemeColorRole role)
{
    switch (role) {
    case ThemeColorRole::window:
        return colors.window;
    case ThemeColorRole::surface:
        return colors.surface;
    case ThemeColorRole::text:
        return colors.text;
    case ThemeColorRole::muted_text:
        return colors.muted_text;
    case ThemeColorRole::disabled_text:
        return colors.disabled_text;
    case ThemeColorRole::accent:
        return colors.accent;
    case ThemeColorRole::accent_text:
        return colors.accent_text;
    case ThemeColorRole::focus:
        return colors.focus;
    case ThemeColorRole::error:
        return colors.error;
    }

    return colors.window;
}

const QColor& theme_color(const ThemeColors& colors, ThemeColorRole role)
{
    return theme_color(const_cast<ThemeColors&>(colors), role);
}

std::span<const ThemeColorDescriptor> theme_color_descriptors()
{
    static constexpr std::array descriptors{
        ThemeColorDescriptor{ThemeColorRole::window,        "window",        "Window"        },
        ThemeColorDescriptor{ThemeColorRole::surface,       "surface",       "Surface"       },
        ThemeColorDescriptor{ThemeColorRole::text,          "text",          "Text"          },
        ThemeColorDescriptor{ThemeColorRole::muted_text,    "muted-text",    "Secondary text"},
        ThemeColorDescriptor{ThemeColorRole::disabled_text, "disabled-text", "Disabled text" },
        ThemeColorDescriptor{ThemeColorRole::accent,        "accent",        "Selection"     },
        ThemeColorDescriptor{ThemeColorRole::accent_text,   "accent-text",   "Selected text" },
        ThemeColorDescriptor{ThemeColorRole::focus,         "focus",         "Keyboard focus"},
        ThemeColorDescriptor{ThemeColorRole::error,         "error",         "Error"         }
    };
    return descriptors;
}

const char* theme_color_key(ThemeColorRole role)
{
    for (const auto& descriptor : theme_color_descriptors())
        if (descriptor.role == role)
            return descriptor.key;
    return "window";
}

double contrast_ratio(const QColor& first, const QColor& second)
{
    const auto luminance = [](const QColor& color) {
        const auto linear = [](qreal value) {
            return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
        };

        return 0.2126 * linear(color.redF()) + 0.7152 * linear(color.greenF()) +
            0.0722 * linear(color.blueF());
    };

    const double a = luminance(first);
    const double b = luminance(second);

    return (std::max(a, b) + 0.05) / (std::min(a, b) + 0.05);
}

QString theme_contrast_error(const ThemeColors& colors)
{
    for (const auto& descriptor : theme_color_descriptors())
        if (!theme_color(colors, descriptor.role).isValid() ||
            theme_color(colors, descriptor.role).alpha() != 255)
            return "Every theme color must be valid and opaque";

    struct Requirement {
        QColor foreground;
        QColor background;
        double minimum;
        const char* message;
    };

    const std::array requirements{
        Requirement{colors.text,          colors.surface, 4.5,
                    "Text needs at least 4.5:1 contrast on the surface"                 },
        Requirement{colors.muted_text,    colors.surface, 4.5,
                    "Secondary text needs at least 4.5:1 contrast on the surface"       },
        Requirement{colors.disabled_text, colors.window,  3.0,
                    "Disabled text needs at least 3:1 contrast on the window"           },
        Requirement{colors.accent_text,   colors.accent,  4.5,
                    "Selected text needs at least 4.5:1 contrast on the selection color"},
        Requirement{colors.focus,         colors.window,  3.0,
                    "Keyboard focus needs at least 3:1 contrast on the window"          },
        Requirement{colors.error,         colors.surface, 4.5,
                    "Errors need at least 4.5:1 contrast on the surface"                },
    };

    for (const Requirement& requirement : requirements) {
        if (!requirement.foreground.isValid() || !requirement.background.isValid())
            return "Every theme color must be valid";

        if (contrast_ratio(requirement.foreground, requirement.background) < requirement.minimum)
            return QString::fromLatin1(requirement.message);
    }

    return {};
}

QPalette theme_palette(const ThemeColors& colors)
{
    QPalette palette;
    const QColor disabled_surface = mix(colors.window, colors.surface, 65);

    for (const QPalette::ColorGroup group : {QPalette::Active, QPalette::Inactive}) {
        palette.setColor(group, QPalette::Window, colors.window);
        palette.setColor(group, QPalette::Base, colors.surface);
        palette.setColor(group, QPalette::AlternateBase, mix(colors.surface, colors.accent, 97));
        palette.setColor(group, QPalette::Button, colors.window);
        palette.setColor(group, QPalette::PlaceholderText, colors.muted_text);
        palette.setColor(group, QPalette::ToolTipBase, colors.surface);
        palette.setColor(group, QPalette::Highlight, colors.accent);
        palette.setColor(group, QPalette::HighlightedText, colors.accent_text);
        palette.setColor(group, QPalette::Link, colors.accent);
        palette.setColor(group, QPalette::Mid, colors.muted_text);
        palette.setColor(group, QPalette::Dark, colors.muted_text);
        set_text_roles(palette, group, colors.text);
    }

    palette.setColor(QPalette::Disabled, QPalette::Window, colors.window);
    palette.setColor(QPalette::Disabled, QPalette::Base, disabled_surface);
    palette.setColor(QPalette::Disabled, QPalette::PlaceholderText, colors.disabled_text);
    palette.setColor(QPalette::Disabled, QPalette::AlternateBase, disabled_surface);
    palette.setColor(QPalette::Disabled, QPalette::Button, colors.window);
    palette.setColor(QPalette::Disabled, QPalette::ToolTipBase, colors.surface);
    palette.setColor(QPalette::Disabled, QPalette::Highlight, disabled_surface);
    palette.setColor(QPalette::Disabled, QPalette::HighlightedText, colors.disabled_text);
    palette.setColor(QPalette::Disabled, QPalette::Mid, colors.disabled_text);
    palette.setColor(QPalette::Disabled, QPalette::Dark, colors.disabled_text);
    set_text_roles(palette, QPalette::Disabled, colors.disabled_text);

    return palette;
}

QIcon themed_standard_icon(const QWidget& styled, QStyle::StandardPixmap icon,
    QPalette::ColorRole color)
{
    return tinted_icon(styled.style()->standardIcon(icon), styled.palette(), color);
}

QIcon tinted_icon(const QIcon& icon, const QPalette& palette, QPalette::ColorRole color)
{
    const QPixmap source = icon.pixmap(24, 24);

    if (source.isNull())
        return {};

    const auto tinted = [&source](const QColor& color) {
        QPixmap result{source.size()};
        result.fill(Qt::transparent);

        QPainter painter{&result};
        painter.drawPixmap(0, 0, source);
        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        painter.fillRect(result.rect(), color);

        return result;
    };

    QIcon result;
    result.addPixmap(tinted(palette.color(QPalette::Active, color)), QIcon::Normal);
    result.addPixmap(tinted(palette.color(QPalette::Disabled, color)), QIcon::Disabled);

    return result;
}

QIcon playlist_activity_icon(const QWidget& styled)
{
    // A shared passive badge, independent of the native platform's icon collection.
    QPixmap image{64, 64};
    image.fill(Qt::transparent);

    QPainter painter{&image};
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(image.width() / 24.0, image.height() / 24.0);

    const auto& palette = styled.palette();
    painter.setBrush(palette.color(QPalette::Highlight));
    painter.setPen(QPen{palette.color(QPalette::HighlightedText), 1.0});
    painter.drawEllipse(QPointF{12.0, 12.0}, 9.0, 9.0);

    QPainterPath check;
    check.moveTo(7.5, 12.0);
    check.lineTo(10.5, 15.0);
    check.lineTo(16.5, 9.0);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen{palette.color(QPalette::HighlightedText), 2.0, Qt::SolidLine, Qt::RoundCap,
        Qt::RoundJoin});
    painter.drawPath(check);
    painter.end();

    QIcon icon;

    for (const auto mode : {QIcon::Normal, QIcon::Active, QIcon::Selected})
        icon.addPixmap(image, mode);

    return icon;
}

QPixmap themed_artwork_placeholder(const QWidget& styled, int size)
{
    QPixmap result{size, size};
    result.fill(styled.palette().color(QPalette::Base));

    QPainter painter{&result};
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen{styled.palette().color(QPalette::Mid),
        static_cast<qreal>(std::max(1, size / 48))});
    painter.drawRoundedRect(QRectF{0.5, 0.5, size - 1.0, size - 1.0}, size / 16.0, size / 16.0);

    const QColor accent = styled.palette().color(QPalette::Highlight);
    painter.setPen(QPen{accent, static_cast<qreal>(std::max(2, size / 24)), Qt::SolidLine,
        Qt::RoundCap});

    const QPointF stem{size * 0.60, size * 0.24};
    painter.drawLine(stem, QPointF{size * 0.60, size * 0.64});
    painter.drawLine(stem, QPointF{size * 0.76, size * 0.30});
    painter.setBrush(accent);
    painter.drawEllipse(QPointF{size * 0.46, size * 0.68}, size * 0.14, size * 0.10);

    return result;
}

void apply_theme(Theme theme)
{
    apply_theme(theme_colors(theme));
}

void apply_theme(const ThemeColors& colors, const ThemeStyle& style)
{
    if (!valid_theme_style(style) || !theme_contrast_error(colors).isEmpty())
        return;

    QApplication::setStyle(make_theme_widget_style());
    QApplication::setPalette(theme_palette(colors));

    QApplication::setFont(theme_font(style));
    qApp->setStyleSheet(theme_stylesheet(colors, style));
}

} // namespace wiola::gui
