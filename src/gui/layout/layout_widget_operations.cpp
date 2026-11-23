/**
 * @file
 * @brief Registered widget factories and recursive layout construction.
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

#include "layout_widget_operations.hpp"
#include <QAction>
#include <QBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabWidget>
#include <QVariant>

#include <algorithm>

namespace wiola::gui::layout_detail {
namespace {

constexpr auto minimum_key = "_wiola_layout_baseline_minimum";
constexpr auto maximum_key = "_wiola_layout_baseline_maximum";
constexpr auto orientation_key = "_wiola_layout_baseline_orientation";

QSize component_minimum(const QWidget& widget)
{
    const auto saved = widget.property(minimum_key);
    return saved.isValid() ? saved.toSize() : widget.minimumSize();
}

} // namespace

bool accepts_dimensions(const QWidget& widget, const LayoutNode& node)
{
    const auto minimum = component_minimum(widget);
    return (!node.width || *node.width >= std::max(minimum.width(), node.min_width.value_or(0))) &&
        (!node.height || *node.height >= std::max(minimum.height(), node.min_height.value_or(0)));
}

void configure_widget(QWidget& widget, const LayoutNode& node)
{
    widget.setObjectName(node.id);
    if (auto* group = qobject_cast<QGroupBox*>(&widget))
        group->setTitle(node.title);
    if (auto* box = qobject_cast<QBoxLayout*>(widget.layout())) {
        if (node.kind == LayoutComponentKind::row)
            box->setDirection(QBoxLayout::LeftToRight);
        else if (node.kind == LayoutComponentKind::column ||
            node.kind == LayoutComponentKind::group)
            box->setDirection(QBoxLayout::TopToBottom);
    }
    if (!widget.property(minimum_key).isValid()) {
        widget.setProperty(minimum_key, widget.minimumSize());
        widget.setProperty(maximum_key, widget.maximumSize());
        if (const auto* splitter = qobject_cast<QSplitter*>(&widget))
            widget.setProperty(orientation_key, static_cast<int>(splitter->orientation()));
    }
    const auto minimum = component_minimum(widget);
    const auto maximum = widget.property(maximum_key).toSize();
    // Reset only constraints owned by layout overrides, preserving the component's real minimums.
    widget.setMinimumSize(0, 0);
    widget.setMaximumSize(maximum);
    widget.setMinimumSize(minimum);
    if (node.min_width)
        widget.setMinimumWidth(std::max(minimum.width(), *node.min_width));
    if (node.min_height)
        widget.setMinimumHeight(std::max(minimum.height(), *node.min_height));
    if (node.width)
        widget.setFixedWidth(*node.width);
    if (node.height)
        widget.setFixedHeight(*node.height);
    if (node.kind == LayoutComponentKind::splitter) {
        auto* splitter = qobject_cast<QSplitter*>(&widget);
        if (!splitter)
            return;
        splitter->setOrientation(node.orientation.value_or(
            static_cast<Qt::Orientation>(widget.property(orientation_key).toInt())
        ));
        splitter->setChildrenCollapsible(false);
        QList<int> sizes;
        if (node.sizes.empty())
            sizes.fill(100, splitter->count());
        else
            for (const auto size : node.sizes)
                sizes.append(size);
        splitter->setSizes(sizes);
    }
}

bool matches_container(QWidget* widget, LayoutComponentKind kind)
{
    switch (kind) {
    case LayoutComponentKind::splitter:
        return qobject_cast<QSplitter*>(widget) != nullptr;
    case LayoutComponentKind::tabs:
        return qobject_cast<QTabWidget*>(widget) != nullptr;
    case LayoutComponentKind::stack:
        return qobject_cast<QStackedWidget*>(widget) != nullptr;
    case LayoutComponentKind::grid:
        return qobject_cast<QGridLayout*>(widget->layout()) != nullptr;
    case LayoutComponentKind::row:
    case LayoutComponentKind::column:
    case LayoutComponentKind::group:
        return qobject_cast<QBoxLayout*>(widget->layout()) != nullptr;
    default:
        return false;
    }
}

bool append_child(QWidget* parent, QWidget* child, const LayoutNode& node,
    const LayoutNode& child_node, std::size_t index)
{
    if (auto* splitter = qobject_cast<QSplitter*>(parent))
        splitter->addWidget(child);
    else if (auto* tabs = qobject_cast<QTabWidget*>(parent))
        tabs->addTab(child, child_node.title.isEmpty() ? child_node.id : child_node.title);
    else if (auto* stack = qobject_cast<QStackedWidget*>(parent))
        stack->addWidget(child);
    else if (auto* grid = qobject_cast<QGridLayout*>(parent->layout())) {
        const auto columns = static_cast<std::size_t>(node.columns.value_or(1));
        grid->addWidget(child, static_cast<int>(index / columns),
            static_cast<int>(index % columns));
    } else if (auto* layout = qobject_cast<QBoxLayout*>(parent->layout()))
        layout->addWidget(child);
    else
        return false;
    return true;
}

void replace_placeholder(QWidget* placeholder, QWidget* widget, QWidget* parent)
{
    QWidget* container = placeholder->parentWidget();
    // QTabWidget pages belong to its private QStackedWidget, not directly to the tabs.
    auto* tabs = container ? qobject_cast<QTabWidget*>(container->parentWidget()) : nullptr;
    if (tabs && tabs->indexOf(placeholder) >= 0) {
        const int index = tabs->indexOf(placeholder);
        const QString title = tabs->tabText(index);
        QWidget* selected = tabs->currentWidget();
        tabs->removeTab(index);
        tabs->insertTab(index, widget, title);
        tabs->setCurrentWidget(selected == placeholder ? widget : selected);
    } else if (auto* splitter = qobject_cast<QSplitter*>(container)) {
        splitter->replaceWidget(splitter->indexOf(placeholder), widget);
    } else if (auto* stack = qobject_cast<QStackedWidget*>(container)) {
        QWidget* selected = stack->currentWidget();
        stack->insertWidget(stack->indexOf(placeholder), widget);
        stack->removeWidget(placeholder);
        stack->setCurrentWidget(selected == placeholder ? widget : selected);
    } else if (container && container->layout()) {
        delete container->layout()->replaceWidget(placeholder, widget, Qt::FindDirectChildrenOnly);
    } else {
        widget->setParent(parent);
    }
}

} // namespace wiola::gui::layout_detail
