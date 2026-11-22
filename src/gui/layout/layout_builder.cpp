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

#include "layout_builder.hpp"
#include <gui/shell/action_button_binding.hpp>

#include <gui/shell/player_actions.hpp>
#include <gui/tracks/track_table_view.hpp>

#include <QAction>
#include <QBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabWidget>

#include <algorithm>

namespace wiola::gui {

namespace {

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
    if (node.width)
        widget.setFixedWidth(*node.width);
    if (node.height)
        widget.setFixedHeight(*node.height);
    if (node.min_width)
        widget.setMinimumWidth(*node.min_width);
    if (node.min_height)
        widget.setMinimumHeight(*node.min_height);
    if (node.kind == LayoutComponentKind::splitter) {
        auto* splitter = qobject_cast<QSplitter*>(&widget);
        if (!splitter)
            return;
        splitter->setOrientation(node.orientation.value_or(Qt::Horizontal));
        splitter->setChildrenCollapsible(false);
        if (!node.sizes.empty()) {
            QList<int> sizes;
            for (int size : node.sizes)
                sizes.append(size);
            splitter->setSizes(sizes);
        }
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

std::size_t bounded_count(const LayoutNode& node, int depth = 0)
{
    if (depth > 16)
        return 257;
    std::size_t count = 1;
    for (const LayoutNode& child : node.children) {
        count += bounded_count(child, depth + 1);
        if (count > 256)
            break;
    }
    return count;
}

} // namespace

LayoutBuilder::LayoutBuilder()
{
    const auto add = [this](LayoutComponentKind kind, Factory factory) {
        static_cast<void>(register_component(kind, std::move(factory)));
    };
    add(LayoutComponentKind::row, [](QWidget* parent, const auto&) {
        auto* widget = new QWidget{parent};
        new QHBoxLayout{widget};
        return widget;
    });
    add(LayoutComponentKind::column, [](QWidget* parent, const auto&) {
        auto* widget = new QWidget{parent};
        new QVBoxLayout{widget};
        return widget;
    });
    add(LayoutComponentKind::stack,
        [](QWidget* parent, const auto&) { return new QStackedWidget{parent}; });
    add(LayoutComponentKind::splitter,
        [](QWidget* parent, const auto&) { return new QSplitter{parent}; });
    add(LayoutComponentKind::tabs,
        [](QWidget* parent, const auto&) { return new QTabWidget{parent}; });
    add(LayoutComponentKind::grid, [](QWidget* parent, const auto&) {
        auto* widget = new QWidget{parent};
        new QGridLayout{widget};
        return widget;
    });
    add(LayoutComponentKind::group, [](QWidget* parent, const auto&) {
        auto* group = new QGroupBox{parent};
        new QVBoxLayout{group};
        return group;
    });
    add(LayoutComponentKind::button,
        [](QWidget* parent, const auto&) { return new QPushButton{parent}; });
    add(LayoutComponentKind::track_table,
        [](QWidget* parent, const auto&) { return new TrackTableView{parent}; });
    for (const LayoutComponentKind kind : {LayoutComponentKind::inspector,
             LayoutComponentKind::search, LayoutComponentKind::tags, LayoutComponentKind::transport,
             LayoutComponentKind::volume, LayoutComponentKind::status})
        add(kind, [](QWidget* parent, const auto&) { return new QLabel{parent}; });
}

bool LayoutBuilder::register_component(LayoutComponentKind kind, Factory factory)
{
    if (!factory || factories_.contains(kind))
        return false;
    factories_.emplace(kind, std::move(factory));
    return true;
}

bool LayoutBuilder::register_widget(const QString& id, QWidget* widget)
{
    if (id.isEmpty() || widget == nullptr || widgets_.contains(id) ||
        std::ranges::any_of(widgets_,
            [widget](const auto& entry) { return entry.second == widget; }))
        return false;
    widgets_.emplace(id, widget);
    return true;
}

QWidget* LayoutBuilder::build_node(const LayoutNode& node, const LayoutBuildContext& context,
    QWidget* parent, std::vector<Attachment>& attachments) const
{
    const auto reused = widgets_.find(node.id);
    QWidget* widget = nullptr;
    if (reused != widgets_.end() && node.children.empty()) {
        // A borrowed leaf may be a composite application view whose contents must stay intact.
        widget = new QWidget{parent};
    } else {
        const auto factory = factories_.find(node.kind);
        if (factory == factories_.end() ||
            (reused != widgets_.end() && !matches_container(reused->second, node.kind)))
            return nullptr;
        widget = factory->second(parent, context);
    }
    if (!widget)
        return nullptr;
    if (reused != widgets_.end() && node.kind == LayoutComponentKind::splitter &&
        !qobject_cast<QSplitter*>(reused->second)) {
        delete widget;
        return nullptr;
    }
    if (reused != widgets_.end())
        attachments.push_back({.placeholder = widget, .widget = reused->second, .node = node});
    else if (node.kind == LayoutComponentKind::button && node.action && context.actions) {
        auto* button = qobject_cast<QPushButton*>(widget);
        QAction* action = context.actions->action(*node.action);
        if (button && action) {
            new ActionButtonBinding{*button, *action};
        }
    }
    for (std::size_t index = 0; index < node.children.size(); ++index) {
        const LayoutNode& child = node.children[index];
        QWidget* nested = build_node(child, context, widget, attachments);
        if (!nested || !append_child(widget, nested, node, child, index)) {
            delete widget;
            return nullptr;
        }
    }
    if (reused == widgets_.end())
        configure_widget(*widget, node);
    return widget;
}

QWidget* LayoutBuilder::build(const LayoutNode& node, const LayoutBuildContext& context,
    QWidget* parent) const
{
    const LayoutDocument document{.version = 1, .root = node, .node_count = bounded_count(node)};
    if (validate_layout_document(document) != LayoutJsonError::none)
        return nullptr;
    std::vector<Attachment> attachments;
    QWidget* root = build_node(node, context, parent, attachments);
    if (!root)
        return nullptr;

    std::vector<std::pair<QWidget*, QWidget*>> selections;
    for (const Attachment& attachment : attachments) {
        if (auto* tabs = qobject_cast<QTabWidget*>(attachment.widget))
            selections.emplace_back(tabs, tabs->currentWidget());
        else if (auto* stack = qobject_cast<QStackedWidget*>(attachment.widget))
            selections.emplace_back(stack, stack->currentWidget());
    }

    // Retire old structural widgets together, preserving any omitted registered views below.
    QWidget retired;
    const auto retire = [&retired](QWidget* widget) {
        widget->setParent(&retired);
        widget->hide();
    };

    // No borrowed widget moves until the entire candidate tree is known to be constructible.
    for (auto attachment = attachments.rbegin(); attachment != attachments.rend(); ++attachment) {
        QWidget* placeholder = attachment->placeholder;
        QWidget* widget = attachment->widget;
        if (!attachment->node.children.empty()) {
            if (auto* target = qobject_cast<QSplitter*>(widget)) {
                auto* source = qobject_cast<QSplitter*>(placeholder);
                while (target->count() != 0) {
                    QWidget* child = target->widget(0);
                    retire(child);
                }
                while (source->count() != 0)
                    target->addWidget(source->widget(0));
            } else if (auto* target = qobject_cast<QTabWidget*>(widget)) {
                auto* source = qobject_cast<QTabWidget*>(placeholder);
                while (target->count() != 0) {
                    QWidget* child = target->widget(0);
                    target->removeTab(0);
                    retire(child);
                }
                while (source->count() != 0) {
                    QWidget* child = source->widget(0);
                    const QString title = source->tabText(0);
                    source->removeTab(0);
                    target->addTab(child, title);
                }
            } else if (auto* target = qobject_cast<QStackedWidget*>(widget)) {
                auto* source = qobject_cast<QStackedWidget*>(placeholder);
                while (target->count() != 0) {
                    QWidget* child = target->widget(0);
                    target->removeWidget(child);
                    retire(child);
                }
                while (source->count() != 0) {
                    QWidget* child = source->widget(0);
                    source->removeWidget(child);
                    target->addWidget(child);
                }
            } else {
                while (QLayoutItem* item = widget->layout()->takeAt(0)) {
                    if (item->widget())
                        retire(item->widget());
                    delete item;
                }
                std::size_t index = 0;
                while (QLayoutItem* item = placeholder->layout()->takeAt(0)) {
                    QWidget* child = item->widget();
                    delete item;
                    // Reused children have already left their original parent during commit.
                    append_child(widget, child, attachment->node, attachment->node.children[index],
                        index);
                    ++index;
                }
            }
        }
        replace_placeholder(placeholder, widget, parent);
        configure_widget(*widget, attachment->node);
        widget->show();
        const bool is_root = placeholder == root;
        delete placeholder;
        if (is_root)
            root = widget;
    }
    for (const auto& [id, widget] : widgets_) {
        if (retired.isAncestorOf(widget)) {
            widget->setParent(parent);
            widget->hide();
        }
    }
    for (const auto& [widget, selected] : selections) {
        if (auto* tabs = qobject_cast<QTabWidget*>(widget); tabs && tabs->indexOf(selected) >= 0)
            tabs->setCurrentWidget(selected);
        else if (auto* stack = qobject_cast<QStackedWidget*>(widget);
            stack && stack->indexOf(selected) >= 0)
            stack->setCurrentWidget(selected);
    }
    return root;
}

} // namespace wiola::gui
