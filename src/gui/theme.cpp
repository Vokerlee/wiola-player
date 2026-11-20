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
#include <QFont>
#include <QPainter>
#include <QWidget>

#include <algorithm>

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

QPalette theme_palette(const ThemeColors& colors)
{
    QPalette palette;
    const QColor disabled_surface = mix(colors.window, colors.surface, 65);

    for (const QPalette::ColorGroup group : {QPalette::Active, QPalette::Inactive}) {
        palette.setColor(group, QPalette::Window, colors.window);
        palette.setColor(group, QPalette::Base, colors.surface);
        palette.setColor(group, QPalette::AlternateBase, mix(colors.surface, colors.accent, 97));
        palette.setColor(group, QPalette::Button, colors.window);
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

QIcon themed_standard_icon(const QWidget& styled, QStyle::StandardPixmap icon)
{
    const QPixmap source = styled.style()->standardIcon(icon).pixmap(24, 24);
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
    result.addPixmap(tinted(styled.palette().color(QPalette::Active, QPalette::ButtonText)),
        QIcon::Normal);
    result.addPixmap(tinted(styled.palette().color(QPalette::Disabled, QPalette::ButtonText)),
        QIcon::Disabled);
    return result;
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
    const ThemeColors colors = theme_colors(theme);
    QApplication::setPalette(theme_palette(colors));

    QFont font = QApplication::font();
    font.setStyleStrategy(QFont::PreferAntialias);
    QApplication::setFont(font);

    constexpr int control_padding = 5;
    qApp->setStyleSheet(QString{
        "QPushButton, QLineEdit, QComboBox, QSpinBox { padding: %1px; }\n"
        "QPushButton:focus, QLineEdit:focus, QComboBox:focus, QSpinBox:focus,"
        " QTableView:focus, QTreeView:focus { border: 1px solid %2; }\n"
        "QLineEdit[error=\"true\"], QComboBox[error=\"true\"] { border: 1px solid %3; }\n"
        "QLabel[status=\"busy\"] { color: %2; }\n"
        "QLabel[status=\"error\"] { color: %3; }\n"
        "QLabel[status=\"ready\"] { color: %6; }\n"
        "QToolTip { color: %4; background-color: %5; border: 1px solid %2; }"}
            .arg(control_padding)
            .arg(colors.focus.name(), colors.error.name(), colors.text.name(),
                colors.surface.name())
            .arg(colors.muted_text.name()));
}

} // namespace wiola::gui
