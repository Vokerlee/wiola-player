/**
 * @file
 * @brief Library-focused and queue-focused player layout presets.
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

#include "focused_layout.hpp"

#include "classic_layout.hpp"

#include <algorithm>
#include <functional>

namespace wiola::gui {

namespace {

LayoutNode component(const QString& id, LayoutComponentKind kind)
{
    LayoutNode node;
    node.id = id;
    node.kind = kind;
    return node;
}

std::size_t count_nodes(const LayoutNode& node)
{
    std::size_t count = 1;

    for (const LayoutNode& child : node.children)
        count += count_nodes(child);

    return count;
}

LayoutNode* document_area(LayoutDocument& document)
{
    const auto found =
        std::ranges::find(document.root.children, QString{"documentArea"}, &LayoutNode::id);

    return found == document.root.children.end() ? nullptr : &*found;
}

} // namespace

LayoutDocument library_focus_layout_document()
{
    LayoutDocument document = classic_layout_document();
    LayoutNode* area = document_area(document);

    if (area == nullptr)
        return {};

    LayoutNode navigation = component("libraryFocusNavigation", LayoutComponentKind::column);
    navigation.children = {
        component("libraryNavigation", LayoutComponentKind::inspector),
        component("savedPlaylistManager", LayoutComponentKind::inspector),
    };

    area->id = "libraryFocusBody";
    area->title.clear();
    area->kind = LayoutComponentKind::row;
    area->children = {
        navigation,
        component("documentTabs", LayoutComponentKind::stack),
        component("trackInspector", LayoutComponentKind::inspector),
    };

    document.root.id = "libraryFocusRoot";
    document.node_count = count_nodes(document.root);
    return document;
}

LayoutDocument queue_focus_layout_document()
{
    LayoutDocument document = classic_layout_document();
    LayoutNode* area = document_area(document);

    if (area == nullptr)
        return {};

    *area = component("documentTabs", LayoutComponentKind::stack);
    document.root.id = "queueFocusRoot";
    document.node_count = count_nodes(document.root);
    return document;
}

} // namespace wiola::gui
