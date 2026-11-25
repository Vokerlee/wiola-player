/**
 * @file
 * @brief Shared actions for every existing player command.
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
#include <QString>
#include <optional>
#include <span>

namespace wiola::gui {

enum class LayoutComponentKind {
    row,
    column,
    stack,
    splitter,
    grid,
    tabs,
    group,
    button,
    track_table,
    inspector,
    search,
    tags,
    transport,
    volume,
    status,
    document_host,
};

enum class ComponentProperty : unsigned {
    title = 1,
    size = 2,
    action = 4,
    orientation = 8,
    weights = 16,
    columns = 32,
};

struct ComponentDescriptor {
    LayoutComponentKind kind;
    const char* key;
    bool container;
    unsigned properties;

    [[nodiscard]] bool supports(ComponentProperty property) const
    {
        return (properties & static_cast<unsigned>(property)) != 0;
    }
};

[[nodiscard]] std::span<const ComponentDescriptor> component_descriptors();
[[nodiscard]] const ComponentDescriptor* component_descriptor(LayoutComponentKind kind);
[[nodiscard]] std::optional<LayoutComponentKind> parse_component(const QString& key);

} // namespace wiola::gui
