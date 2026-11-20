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

#include "player_actions.hpp"
#include "track_table_view.hpp"

#include <QAction>
#include <QBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>

namespace wiola::gui {

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
    if (id.isEmpty() || widget == nullptr || widgets_.contains(id))
        return false;

    widgets_.emplace(id, widget);
    return true;
}

QWidget* LayoutBuilder::build(const LayoutNode& node, const LayoutBuildContext& context,
    QWidget* parent) const
{
    const auto reused = widgets_.find(node.id);
    QWidget* widget = nullptr;

    if (reused != widgets_.end()) {
        widget = reused->second;
    } else {
        const auto registered = factories_.find(node.kind);
        if (registered == factories_.end())
            return nullptr;
        widget = registered->second(parent, context);
    }

    if (widget == nullptr)
        return nullptr;

    widget->setObjectName(node.id);

    if (auto* group = qobject_cast<QGroupBox*>(widget))
        group->setTitle(node.title);

    if (node.width)
        widget->setFixedWidth(*node.width);
    if (node.height)
        widget->setFixedHeight(*node.height);
    if (node.min_width)
        widget->setMinimumWidth(*node.min_width);
    if (node.min_height)
        widget->setMinimumHeight(*node.min_height);

    if (reused == widgets_.end() && node.kind == LayoutComponentKind::button && node.action &&
        context.actions) {
        auto* button = qobject_cast<QPushButton*>(widget);
        QAction* action = context.actions->action(*node.action);

        if (button && action) {
            button->setText(action->text());
            button->setToolTip(action->toolTip());
            button->setAccessibleName(action->text());
            button->setCheckable(action->isCheckable());
            button->setChecked(action->isChecked());
            button->setEnabled(action->isEnabled());
            QObject::connect(button, &QPushButton::clicked, action, &QAction::trigger);
        }
    }

    for (const LayoutNode& child : node.children) {
        QWidget* nested = build(child, context, widget);
        if (nested == nullptr) {
            delete widget;
            return nullptr;
        }

        if (auto* stack = qobject_cast<QStackedWidget*>(widget)) {
            stack->addWidget(nested);
        } else if (auto* layout = qobject_cast<QBoxLayout*>(widget->layout())) {
            layout->addWidget(nested);
        } else {
            delete widget;
            return nullptr;
        }
    }

    return widget;
}

} // namespace wiola::gui
