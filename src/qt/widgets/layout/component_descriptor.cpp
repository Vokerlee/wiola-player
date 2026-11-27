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
    using enum LayoutComponentKind;

    static constexpr std::array descriptors{
        ComponentDescriptor{row,           "row",           "Horizontal row",    true,  3 | 64    },
        ComponentDescriptor{column,        "column",        "Vertical column",   true,  3         },
        ComponentDescriptor{stack,         "stack",         "Stacked pages",     true,  3         },
        ComponentDescriptor{splitter,      "splitter",      "Resizable panes",   true,  3 | 8 | 16},
        ComponentDescriptor{grid,          "grid",          "Grid",              true,  3 | 32    },
        ComponentDescriptor{tabs,          "tabs",          "Tabs",              true,  3         },
        ComponentDescriptor{group,         "group",         "Labeled group",     true,  3         },
        ComponentDescriptor{button,        "button",        "Button",            false, 3 | 4     },
        ComponentDescriptor{track_table,   "track_table",   "Track list",        false, 3         },
        ComponentDescriptor{inspector,     "inspector",     "Track details",     false, 3         },
        ComponentDescriptor{search,        "search",        "Search",            false, 3         },
        ComponentDescriptor{tags,          "tags",          "Tag filters",       false, 3         },
        ComponentDescriptor{transport,     "transport",     "Playback timeline", false, 3         },
        ComponentDescriptor{volume,        "volume",        "Volume",            false, 3         },
        ComponentDescriptor{status,        "status",        "Text",              false, 3         },
        ComponentDescriptor{document_host, "document_host", "Playlist tabs",     false, 3         }
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
