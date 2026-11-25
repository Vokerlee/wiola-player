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
#include "layout_tree.hpp"
#include "layout_widget_operations.hpp"
#include <qt/widgets/shell/action_button_binding.hpp>

#include <qt/widgets/shell/player_actions.hpp>
#include <qt/widgets/tracks/track_table_view.hpp>

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

using namespace layout_detail;

namespace {

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
    for (const LayoutComponentKind kind :
        {LayoutComponentKind::inspector, LayoutComponentKind::search, LayoutComponentKind::tags,
            LayoutComponentKind::transport, LayoutComponentKind::volume,
            LayoutComponentKind::status, LayoutComponentKind::document_host})
        add(kind, [](QWidget* parent, const auto&) { return new QLabel{parent}; });
}

bool LayoutBuilder::register_component(LayoutComponentKind kind, Factory factory)
{
    if (!factory || !component_descriptor(kind) || factories_.contains(kind))
        return false;
    factories_.emplace(kind, std::move(factory));
    return true;
}

QWidget* LayoutBuilder::build_node(const LayoutNode& node, const LayoutBuildContext& context,
    QWidget* parent, std::vector<LayoutAttachment>& attachments) const
{
    const auto* registration =
        node.view && !context.isolated_preview ? views_.find(*node.view) : nullptr;
    if (node.view && !context.isolated_preview &&
        (!registration || registration->kind != node.kind ||
            (!node.children.empty() && !registration->allow_structure)))
        return nullptr;
    QWidget* reused = registration ? registration->widget.data() : nullptr;
    QWidget* widget = nullptr;
    if (reused != nullptr && node.children.empty()) {
        // A borrowed leaf may be a composite application view whose contents must stay intact.
        widget = new QWidget{parent};
    } else {
        const auto factory = factories_.find(node.kind);
        if (factory == factories_.end() ||
            (reused != nullptr && !matches_container(reused, node.kind)))
            return nullptr;
        widget = factory->second(parent, context);
    }
    if (!widget)
        return nullptr;
    if (!reused && !accepts_dimensions(*widget, node)) {
        delete widget;
        return nullptr;
    }
    if (reused != nullptr && node.kind == LayoutComponentKind::splitter &&
        !qobject_cast<QSplitter*>(reused)) {
        delete widget;
        return nullptr;
    }
    if (reused != nullptr)
        attachments.push_back({.placeholder = widget,
            .widget = reused,
            .node = node,
            .original_parent = reused->parentWidget()});
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
    if (reused == nullptr)
        configure_widget(*widget, node);
    return widget;
}

std::unique_ptr<PreparedLayout> LayoutBuilder::prepare(const LayoutNode& node,
    const LayoutBuildContext& context, QWidget* parent) const
{
    const LayoutDocument document{.version = 2,
        .root = node,
        .node_count = layout_node_count(node)};
    if (validate_layout_document(document) != LayoutJsonError::none)
        return nullptr;
    const auto preflight = [&](auto&& self, const LayoutNode& current) -> bool {
        if (current.view && !context.isolated_preview) {
            const auto* view = views_.find(*current.view);
            if (!view || !accepts_dimensions(*view->widget, current) ||
                view->kind != current.kind ||
                (parent && (view->widget == parent || view->widget->isAncestorOf(parent))) ||
                (!current.children.empty() && !view->allow_structure))
                return false;
        }
        return std::ranges::all_of(current.children,
            [&](const auto& child) { return self(self, child); });
    };
    if (!preflight(preflight, node))
        return nullptr;
    std::vector<LayoutAttachment> attachments;
    QWidget* root = build_node(node, context, nullptr, attachments);
    if (!root)
        return nullptr;

    return std::unique_ptr<PreparedLayout>{
        new PreparedLayout{std::unique_ptr<QWidget>{root}, std::move(attachments), views_,
                           document}
    };
}

QWidget* LayoutBuilder::build(const LayoutNode& node, const LayoutBuildContext& context,
    QWidget* parent) const
{
    auto prepared = prepare(node, context, parent);
    return prepared ? prepared->attach(parent) : nullptr;
}

} // namespace wiola::gui
