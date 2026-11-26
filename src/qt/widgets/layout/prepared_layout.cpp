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

#include "prepared_layout.hpp"
#include "layout_widget_operations.hpp"
#include <QApplication>
#include <QLayout>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabWidget>
#include <algorithm>

namespace wiola::gui {

using namespace layout_detail;

bool PreparedLayout::ready(QWidget* parent) const
{
    if (!root_)
        return false;
    for (const auto& attachment : attachments_) {
        if (!attachment.widget || !accepts_dimensions(*attachment.widget, attachment.node) ||
            attachment.widget->parentWidget() != attachment.original_parent ||
            (parent && (attachment.widget == parent || attachment.widget->isAncestorOf(parent))))
            return false;
    }
    return true;
}

QWidget* PreparedLayout::attach(QWidget* parent)
{
    if (!ready(parent))
        return nullptr;
    QPointer<QWidget> focus = QApplication::focusWidget();
    std::vector<QSignalBlocker> blocked;
    blocked.reserve(attachments_.size());
    for (const auto& attachment : attachments_)
        blocked.emplace_back(attachment.widget.data());
    QWidget* root = root_.release();
    if (parent)
        root->setParent(parent);
    std::vector<std::pair<QWidget*, QWidget*>> selections;
    for (const LayoutAttachment& attachment : attachments_) {
        if (auto* tabs = qobject_cast<QTabWidget*>(attachment.widget.data()))
            selections.emplace_back(tabs, tabs->currentWidget());
        else if (auto* stack = qobject_cast<QStackedWidget*>(attachment.widget.data()))
            selections.emplace_back(stack, stack->currentWidget());
    }

    // Retire old structural widgets together, preserving any omitted registered views below.
    QWidget retired;
    const auto retire = [&retired](QWidget* widget) {
        widget->setParent(&retired);
        widget->hide();
    };

    // A splitter omitted by the new layout must not retain old decorative containers.
    // Registered descendants are recovered below before the retired hierarchy is destroyed.
    for (const auto& [id, registration] : views_.entries()) {
        auto* splitter = qobject_cast<QSplitter*>(registration.widget.data());
        if (!splitter || std::ranges::any_of(attachments_, [splitter](const auto& attachment) {
                return attachment.widget == splitter;
            }))
            continue;
        while (splitter->count() != 0)
            retire(splitter->widget(0));
    }

    // No borrowed widget moves until the entire candidate tree is known to be constructible.
    for (auto attachment = attachments_.rbegin(); attachment != attachments_.rend(); ++attachment) {
        QWidget* placeholder = attachment->placeholder;
        QWidget* widget = attachment->widget.data();
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
    for (const auto& [id, registration] : views_.entries()) {
        QWidget* widget = registration.widget.data();
        if (!widget)
            continue;
        if (retired.isAncestorOf(widget)) {
            bool covered = false;
            for (auto* ancestor = widget->parentWidget(); ancestor && ancestor != &retired;
                ancestor = ancestor->parentWidget())
                if (std::ranges::any_of(views_.entries(), [ancestor](const auto& entry) {
                        return entry.second.widget == ancestor;
                    })) {
                    covered = true;
                    break;
                }
            if (!covered) {
                widget->setParent(parent);
                widget->hide();
            }
        }
    }
    for (const auto& [widget, selected] : selections) {
        if (auto* tabs = qobject_cast<QTabWidget*>(widget); tabs && tabs->indexOf(selected) >= 0)
            tabs->setCurrentWidget(selected);
        else if (auto* stack = qobject_cast<QStackedWidget*>(widget);
            stack && stack->indexOf(selected) >= 0)
            stack->setCurrentWidget(selected);
    }
    attachments_.clear();
    if (focus && (focus == root || root->isAncestorOf(focus)))
        focus->setFocus(Qt::OtherFocusReason);
    return root;
}

} // namespace wiola::gui
