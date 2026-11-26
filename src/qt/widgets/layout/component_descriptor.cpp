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

#include "component_descriptor.hpp"
#include <array>

namespace wiola::gui {

std::span<const ComponentDescriptor> component_descriptors()
{
    static constexpr std::array descriptors{
        ComponentDescriptor{LayoutComponentKind::row,           "row",           true,  3 | 64    },
        ComponentDescriptor{LayoutComponentKind::column,        "column",        true,  3         },
        ComponentDescriptor{LayoutComponentKind::stack,         "stack",         true,  3         },
        ComponentDescriptor{LayoutComponentKind::splitter,      "splitter",      true,  3 | 8 | 16},
        ComponentDescriptor{LayoutComponentKind::grid,          "grid",          true,  3 | 32    },
        ComponentDescriptor{LayoutComponentKind::tabs,          "tabs",          true,  3         },
        ComponentDescriptor{LayoutComponentKind::group,         "group",         true,  3         },
        ComponentDescriptor{LayoutComponentKind::button,        "button",        false, 3 | 4     },
        ComponentDescriptor{LayoutComponentKind::track_table,   "track_table",   false, 3         },
        ComponentDescriptor{LayoutComponentKind::inspector,     "inspector",     false, 3         },
        ComponentDescriptor{LayoutComponentKind::search,        "search",        false, 3         },
        ComponentDescriptor{LayoutComponentKind::tags,          "tags",          false, 3         },
        ComponentDescriptor{LayoutComponentKind::transport,     "transport",     false, 3         },
        ComponentDescriptor{LayoutComponentKind::volume,        "volume",        false, 3         },
        ComponentDescriptor{LayoutComponentKind::status,        "status",        false, 3         },
        ComponentDescriptor{LayoutComponentKind::document_host, "document_host", false, 3         }
    };
    return descriptors;
}

const ComponentDescriptor* component_descriptor(LayoutComponentKind kind)
{
    for (const auto& descriptor : component_descriptors())
        if (descriptor.kind == kind)
            return &descriptor;
    return nullptr;
}

std::optional<LayoutComponentKind> parse_component(const QString& key)
{
    for (const auto& descriptor : component_descriptors())
        if (key == descriptor.key)
            return descriptor.kind;
    return {};
}

} // namespace wiola::gui
