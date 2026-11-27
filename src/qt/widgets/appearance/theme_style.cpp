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

#include "theme.hpp"
#include "theme_style.hpp"

#include <QFontDatabase>
#include <QPainter>
#include <QProxyStyle>
#include <QStyleFactory>
#include <QStyleOption>

#include <algorithm>
#include <array>

namespace wiola::gui {
namespace {

class ModernWidgetStyle final : public QProxyStyle {
public:
    ModernWidgetStyle()
        : QProxyStyle{QStyleFactory::create("Fusion")}
    {
    }

    void drawPrimitive(PrimitiveElement element, const QStyleOption* option, QPainter* painter,
        const QWidget* widget = nullptr) const override
    {
        if (element != PE_IndicatorTabClose) {
            QProxyStyle::drawPrimitive(element, option, painter, widget);
            return;
        }

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);

        const auto group =
            option->state.testFlag(State_Enabled) ? QPalette::Active : QPalette::Disabled;
        painter->setPen(QPen{option->palette.color(group, QPalette::ButtonText), 1.5, Qt::SolidLine,
            Qt::RoundCap});

        const auto bounds = QRectF{option->rect};
        const auto inset = std::min(bounds.width(), bounds.height()) * 0.28;
        const auto cross = bounds.adjusted(inset, inset, -inset, -inset);
        painter->drawLine(cross.topLeft(), cross.bottomRight());
        painter->drawLine(cross.topRight(), cross.bottomLeft());
        painter->restore();
    }
};

} // namespace

QStyle* make_theme_widget_style()
{
    return new ModernWidgetStyle;
}

std::span<const ThemeStyleMetric> theme_style_metrics()
{
    static constexpr std::array metrics{
        ThemeStyleMetric{"radius",     "Corner radius",   " px", &ThemeStyle::corner_radius,   0,  20 },
        ThemeStyleMetric{"padding",    "Control spacing", " px", &ThemeStyle::control_padding, 2,  12 },
        ThemeStyleMetric{"text-scale", "Text size",       " %",  &ThemeStyle::text_scale,      85, 125},
    };
    return metrics;
}

bool valid_theme_style(const ThemeStyle& style)
{
    return std::ranges::all_of(theme_style_metrics(), [&style](const auto& metric) {
        return style.*metric.value >= metric.minimum && style.*metric.value <= metric.maximum;
    });
}

QFont theme_font(const ThemeStyle& style)
{
    QFont font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);

    if (font.pointSizeF() > 0)
        font.setPointSizeF(font.pointSizeF() * style.text_scale / 100.0);

    font.setStyleStrategy(QFont::PreferAntialias);

    return font;
}

QString theme_stylesheet(const ThemeColors& colors, const ThemeStyle& style)
{
    QString sheet{R"(
QPushButton, QToolButton {
    background-color: @surface@; color: @text@;
    border: 1px solid @border@; border-radius: @radius@px;
    padding: @padding@px @wide-padding@px; min-height: 1.2em;
}
QPushButton:enabled:hover, QToolButton:enabled:hover { background-color: @hover@; border-color: @muted-text@; }
QPushButton:pressed, QToolButton:pressed { background-color: @pressed@; }
QPushButton:checked, QToolButton:checked, QPushButton[appearanceRole="primary"] {
    background-color: @accent@; color: @accent-text@; border-color: @accent@;
}
QPushButton[appearanceRole="primary"]:enabled:hover { background-color: @accent@; border-color: @focus@; }
QPushButton:disabled, QToolButton:disabled {
    background-color: @window@; color: @disabled-text@; border-color: @border@;
}
QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox {
    background-color: @surface@; color: @text@;
    border: 1px solid @border@; border-radius: @radius@px;
    padding: @padding@px; min-height: 1.2em;
    selection-background-color: @accent@; selection-color: @accent-text@;
}
QLineEdit:disabled, QComboBox:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled {
    color: @disabled-text@; background-color: @window@;
}
QPushButton:focus, QToolButton:focus, QLineEdit:focus, QComboBox:focus,
QSpinBox:focus, QDoubleSpinBox:focus { border: 1px solid @focus@; }
QLineEdit[error="true"], QComboBox[error="true"] { border-color: @error@; }
QTabWidget::pane { border: none; }
QTabBar { qproperty-drawBase: 0; }
QTabBar::tab {
    background: transparent; color: @muted-text@; border: none;
    border-bottom: 2px solid transparent; padding: @padding@px @wide-padding@px;
    margin-right: 4px;
}
QTabBar::tab:hover { background: @hover@; color: @text@; }
QTabBar::tab:selected {
    background: @surface@; color: @text@; border-bottom: 2px solid @accent@;
    border-top-left-radius: @radius@px; border-top-right-radius: @radius@px;
}
QHeaderView::section {
    background: @window@; color: @muted-text@; border: none;
    border-bottom: 1px solid @border@; padding: @padding@px; font-weight: 600;
}
QTableView, QTreeView {
    border: none; background: @surface@; color: @text@;
    selection-background-color: @accent@; selection-color: @accent-text@;
}
QTreeView::item { padding: 4px 0; }
QTreeView::item:hover, QTableView::item:hover { background-color: @hover@; }
QTreeView::item:selected, QTableView::item:selected { background-color: @accent@; color: @accent-text@; }
QGroupBox { border: 1px solid @border@; border-radius: @radius@px; margin-top: 1em; padding: @padding@px; }
QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; padding: 0 6px; }
QGroupBox[appearanceRole="transport"] {
    background: @surface@; border: none; border-top: 1px solid @border@;
    border-radius: 0; margin-top: 0; padding: @padding@px;
}
QMenuBar { background: @window@; color: @muted-text@; padding: 2px; }
QMenuBar::item { padding: 4px 8px; background: transparent; }
QMenuBar::item:selected { background: @hover@; color: @text@; border-radius: 4px; }
QMenu {
    background: @surface@; color: @text@; border: 1px solid @border@;
    border-radius: @radius@px; padding: 4px;
}
QMenu::item { padding: @padding@px 24px; border-radius: 4px; }
QMenu::item:selected { background: @accent@; color: @accent-text@; }
QMenu::item:disabled { color: @disabled-text@; }
QMenu::separator { height: 1px; background: @border@; margin: 4px 8px; }
QDockWidget::title { background: @window@; color: @muted-text@; padding: 4px; }
QStatusBar::item { border: none; }
QMainWindow::separator { width: 8px; height: 8px; background: @window@; border-left: 1px solid @border@; }
QMainWindow::separator:hover { background: @accent@; }
QSplitter::handle { background: @window@; }
QSplitter::handle:hover { background: @border@; }
QSlider::groove:horizontal { height: 4px; background: @border@; border-radius: 2px; }
QSlider::sub-page:horizontal { background: @accent@; border-radius: 2px; }
QSlider::handle:horizontal { width: 12px; margin: -4px 0; background: @text@; border-radius: 6px; }
QSlider::handle:horizontal:hover { background: @accent@; }
QSlider::handle:horizontal:disabled { background: @disabled-text@; }
QScrollBar:vertical { width: 10px; margin: 0; background: transparent; }
QScrollBar:horizontal { height: 10px; margin: 0; background: transparent; }
QScrollBar::handle { background: @border@; border-radius: 5px; }
QScrollBar::handle:hover { background: @muted-text@; }
QScrollBar::handle:vertical { min-height: 24px; }
QScrollBar::handle:horizontal { min-width: 24px; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
QLabel[status="busy"] { color: @focus@; }
QLabel[status="error"] { color: @error@; }
QLabel[status="ready"] { color: @muted-text@; }
QToolTip { color: @text@; background: @surface@; border: 1px solid @border@; padding: 6px; }
)"};

    for (const auto& metric : theme_style_metrics())
        sheet.replace('@' + QString::fromLatin1(metric.key) + '@',
            QString::number(style.*metric.value));

    sheet.replace("@wide-padding@", QString::number(style.control_padding * 2));

    for (const auto& descriptor : theme_color_descriptors())
        sheet.replace('@' + QString::fromLatin1(descriptor.key) + '@',
            theme_color(colors, descriptor.role).name());

    const auto blend = [&colors](int percent) {
        const int inverse = 100 - percent;
        return QColor{(colors.text.red() * percent + colors.surface.red() * inverse) / 100,
            (colors.text.green() * percent + colors.surface.green() * inverse) / 100,
            (colors.text.blue() * percent + colors.surface.blue() * inverse) / 100}
            .name();
    };
    sheet.replace("@border@", blend(20));
    sheet.replace("@hover@", blend(8));
    sheet.replace("@pressed@", blend(14));

    return sheet;
}

} // namespace wiola::gui
