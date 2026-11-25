/**
 * @file
 * @brief Bounded versioned JSON description of a player layout.
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

#include "view_registry.hpp"
#include <algorithm>

namespace wiola::gui {

bool ViewRegistry::add(ViewReference reference, QWidget* widget, LayoutComponentKind kind,
    bool structure)
{
    if (!widget || !valid_view_reference(reference) || !component_descriptor(kind) ||
        (structure && !component_descriptor(kind)->container))
        return false;
    if (const auto old = views_.find(reference); old != views_.end()) {
        if (old->second.widget)
            return false;
        views_.erase(old);
    }
    if (std::ranges::any_of(views_,
            [widget](const auto& entry) { return entry.second.widget == widget; }))
        return false;
    views_.emplace(std::move(reference), ViewRegistration{widget, kind, structure});
    return true;
}

const ViewRegistration* ViewRegistry::find(const ViewReference& reference) const
{
    const auto found = views_.find(reference);
    return found == views_.end() || !found->second.widget ? nullptr : &found->second;
}

} // namespace wiola::gui
