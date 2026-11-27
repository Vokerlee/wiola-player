/**
 * @file
 * @brief Interactive palette, hierarchy, and inspector for button layouts.
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

#include "layout_tree_views.hpp"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QHeaderView>
#include <QMimeData>
#include <QSignalBlocker>

namespace wiola::gui {
namespace {

constexpr auto component_mime = "application/x-wiola-layout-component";
constexpr int identity_role = Qt::UserRole, action_role = Qt::UserRole + 1;

QMimeData* component_data(const QString& payload)
{
    auto* data = new QMimeData;
    data->setData(component_mime, payload.toUtf8());
    data->setText(payload);

    return data;
}

std::optional<LayoutDrop> drop_request(const QMimeData& data, QPoint position)
{
    if (!data.hasFormat(component_mime))
        return {};

    const auto encoded = data.data(component_mime);

    if (encoded.size() > 256)
        return {};

    const auto payload = QString::fromUtf8(encoded);

    if (payload.startsWith("action:")) {
        if (const auto action = parse_action(payload.mid(7)))
            return LayoutDrop{*action, position};
    } else if (payload.startsWith("node:") && payload.size() > 5 &&
        payload.mid(5).toUtf8().size() <= 64)
        return LayoutDrop{payload.mid(5), position};

    return {};
}

} // namespace

ComponentPalette::ComponentPalette(QWidget* parent)
    : QListWidget{parent}
{
    setObjectName("layoutComponentPalette");
    setDragEnabled(true);
    setDragDropMode(QAbstractItemView::DragOnly);
    setAccessibleName("Button action palette");

    for (const auto& entry : action_descriptors()) {
        auto* item = new QListWidgetItem{entry.label, this};
        item->setData(action_role, entry.key);
        item->setToolTip("Drag onto a container, or select it and press Add");
    }
}

QMimeData* ComponentPalette::mimeData(const QList<QListWidgetItem*>& items) const
{
    return items.empty() ? nullptr
                         : component_data("action:" + items.front()->data(action_role).toString());
}

LayoutHierarchy::LayoutHierarchy(QWidget* parent)
    : QTreeWidget{parent}
{
    setObjectName("layoutHierarchy");
    setHeaderLabels({"Component", "Type"});
    header()->setStretchLastSection(false);
    header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setDragEnabled(true);
    setAcceptDrops(true);
    setDropIndicatorShown(true);
    setDragDropMode(QAbstractItemView::DragDrop);
    setAccessibleName("Layout component hierarchy");
}

QStringList LayoutHierarchy::mimeTypes() const
{
    return {QString::fromLatin1(component_mime)};
}

QMimeData* LayoutHierarchy::mimeData(const QList<QTreeWidgetItem*>& items) const
{
    return items.empty()
        ? nullptr
        : component_data("node:" + items.front()->data(0, identity_role).toString());
}

void LayoutHierarchy::dragEnterEvent(QDragEnterEvent* event)
{
    if (drop_request(*event->mimeData(), {}))
        event->acceptProposedAction();
}

void LayoutHierarchy::dragMoveEvent(QDragMoveEvent* event)
{
    if (drop_request(*event->mimeData(), {}))
        event->acceptProposedAction();
}

void LayoutHierarchy::dropEvent(QDropEvent* event)
{
    const auto request = drop_request(*event->mimeData(), event->position().toPoint());

    if (!request || !observer_)
        return;

    const auto notify = observer_;

    event->acceptProposedAction();
    notify(*request);
}

QString LayoutHierarchy::selected_id() const
{
    const auto selected = selectedItems();
    return selected.empty() ? QString{} : selected.front()->data(0, identity_role).toString();
}

void LayoutHierarchy::show_document(const LayoutDocument& document, const QString& selected)
{
    const QSignalBlocker blocked{this};

    clear();

    const auto add_node = [&](auto&& self, const LayoutNode& node,
                              QTreeWidgetItem* parent) -> void {
        auto* item = new QTreeWidgetItem{
            {node.id, layout_component_name(node.kind)}
        };
        item->setData(0, identity_role, node.id);
        item->setToolTip(0, node.id);

        if (parent)
            parent->addChild(item);
        else
            addTopLevelItem(item);

        for (const auto& child : node.children)
            self(self, child, item);
    };
    add_node(add_node, document.root, nullptr);
    expandAll();

    const auto items = findItems(selected, Qt::MatchExactly | Qt::MatchRecursive);
    setCurrentItem(items.empty() ? topLevelItem(0) : items.front());
}

} // namespace wiola::gui
