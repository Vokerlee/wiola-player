/**
 * @file
 * @brief Transactional layout document edits with bounded undo history.
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

#include "layout_editor.hpp"
#include "layout_tree.hpp"

#include <algorithm>
#include <functional>
#include <utility>

namespace wiola::gui {

namespace {

constexpr std::size_t max_history{64};

void push_history(std::vector<LayoutDocument>& history, const LayoutDocument& document)
{
    if (history.size() == max_history)
        history.erase(history.begin());

    history.push_back(document);
}

} // namespace

LayoutEditor::LayoutEditor(LayoutDocument document)
    : document_{std::move(document)}
{
}

bool LayoutEditor::insert(const QString& parent, std::size_t index, const LayoutNode& node)
{
    LayoutDocument next = document_;
    LayoutNode* destination = locate_layout_node(next.root, parent).node;

    if (destination == nullptr || index > destination->children.size())
        return false;

    destination->children.insert(destination->children.begin() + static_cast<std::ptrdiff_t>(index),
        node);
    if (!destination->sizes.empty())
        destination->sizes.insert(destination->sizes.begin() + static_cast<std::ptrdiff_t>(index),
            1);
    return commit(std::move(next));
}

bool LayoutEditor::remove(const QString& id)
{
    if (document_.root.id == id)
        return false;

    LayoutDocument next = document_;
    const auto location = locate_layout_node(next.root, id);
    auto* parent = location.parent;
    const auto index = location.index;

    if (parent == nullptr)
        return false;

    parent->children.erase(parent->children.begin() + static_cast<std::ptrdiff_t>(index));
    if (!parent->sizes.empty())
        parent->sizes.erase(parent->sizes.begin() + static_cast<std::ptrdiff_t>(index));
    return commit(std::move(next));
}

bool LayoutEditor::move(const QString& id, const QString& parent, std::size_t index)
{
    if (document_.root.id == id || id == parent)
        return false;

    LayoutDocument next = document_;
    const auto location = locate_layout_node(next.root, id);
    auto* source = location.parent;
    const auto source_index = location.index;

    if (source == nullptr)
        return false;

    LayoutNode moved = source->children[source_index];
    const bool weighted_source = !source->sizes.empty();
    const int weight = weighted_source ? source->sizes[source_index] : 1;
    source->children.erase(source->children.begin() + static_cast<std::ptrdiff_t>(source_index));
    if (!source->sizes.empty())
        source->sizes.erase(source->sizes.begin() + static_cast<std::ptrdiff_t>(source_index));
    LayoutNode* destination = locate_layout_node(next.root, parent).node;

    if (destination == nullptr || index > destination->children.size())
        return false;

    destination->children.insert(destination->children.begin() + static_cast<std::ptrdiff_t>(index),
        std::move(moved));
    if (!destination->sizes.empty() || (destination == source && weighted_source))
        destination->sizes.insert(destination->sizes.begin() + static_cast<std::ptrdiff_t>(index),
            weight);
    return commit(std::move(next));
}

bool LayoutEditor::configure(const QString& id, const LayoutNode& configuration)
{
    if (configuration.id != id)
        return false;

    LayoutDocument next = document_;
    LayoutNode* node = locate_layout_node(next.root, id).node;

    if (node == nullptr)
        return false;

    LayoutNode configured = configuration;
    configured.children = std::move(node->children);
    *node = std::move(configured);
    return commit(std::move(next));
}

bool LayoutEditor::commit(LayoutDocument next)
{
    next.node_count = layout_node_count(next.root);

    if (validate_layout_document(next) != LayoutJsonError::none)
        return false;

    push_history(undo_, document_);
    document_ = std::move(next);
    redo_.clear();
    return true;
}

bool LayoutEditor::undo()
{
    if (undo_.empty())
        return false;

    push_history(redo_, document_);
    document_ = std::move(undo_.back());
    undo_.pop_back();
    return true;
}

bool LayoutEditor::redo()
{
    if (redo_.empty())
        return false;

    push_history(undo_, document_);
    document_ = std::move(redo_.back());
    redo_.pop_back();
    return true;
}

} // namespace wiola::gui
