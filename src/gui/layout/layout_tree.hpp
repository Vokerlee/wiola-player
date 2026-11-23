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

#pragma once
#include "layout_document.hpp"

namespace wiola::gui {

template<class Node>
struct LayoutNodeLocation {
    Node* parent{};
    Node* node{};
    std::size_t index{};
};

/// Shared bounded lookup for the validated edit model and its presentation adapters.
template<class Node>
[[nodiscard]] LayoutNodeLocation<Node> locate_layout_node(Node& root, const QString& id)
{
    const auto walk = [&](auto&& self, Node& node, Node* parent, std::size_t index,
                          int depth) -> LayoutNodeLocation<Node> {
        if (depth > 16)
            return {};
        if (node.id == id)
            return {parent, &node, index};
        for (std::size_t i = 0; i < node.children.size() && i < 32; ++i)
            if (auto found = self(self, node.children[i], &node, i, depth + 1); found.node)
                return found;
        return {};
    };
    return walk(walk, root, nullptr, 0, 0);
}

[[nodiscard]] inline std::size_t layout_node_count(const LayoutNode& root, int depth = 0)
{
    if (depth > 16)
        return 257;
    std::size_t count = 1;
    for (const auto& child : root.children) {
        count += layout_node_count(child, depth + 1);
        if (count > 256)
            return 257;
    }
    return count;
}

} // namespace wiola::gui
