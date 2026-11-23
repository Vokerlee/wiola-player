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

#pragma once
#include "layout_document.hpp"
class QWidget;

namespace wiola::gui::layout_detail {

void configure_widget(QWidget& widget, const LayoutNode& node);
[[nodiscard]] bool matches_container(QWidget* widget, LayoutComponentKind kind);
[[nodiscard]] bool append_child(QWidget* parent, QWidget* child, const LayoutNode& node,
    const LayoutNode& child_node, std::size_t index);
void replace_placeholder(QWidget* placeholder, QWidget* widget, QWidget* parent);

} // namespace wiola::gui::layout_detail
