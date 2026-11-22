/**
 * @file
 * @brief One normalized search, filter, and sort state for each track browser.
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

#include "track_view_state.hpp"
#include <algorithm>
#include <library/tags.hpp>

namespace wiola::gui {

TrackViewState normalized_track_view_state(TrackViewState state)
{
    state.query = library::normalized_search_query(std::move(state.query));
    std::vector<library::TagFilter> filters;
    for (auto& filter : state.query.filters) {
        if (!library::make_tag_value(filter.key, "value") ||
            (!filter.value.empty() && !library::make_tag_value("key", filter.value)))
            continue;
        if (std::ranges::find(filters, filter) == filters.end())
            filters.push_back(std::move(filter));
    }
    state.query.filters = std::move(filters);
    std::vector<TrackSortKey> keys;
    for (auto key : state.sort_keys) {
        if (!key.valid())
            continue;
        if (key.field == TrackSortField::custom_tag)
            key.tag_key = library::normalized_tag_text(key.tag_key);
        if (std::ranges::none_of(keys, [&](const auto& present) {
                return present.field == key.field && present.tag_key == key.tag_key;
            }))
            keys.push_back(std::move(key));
    }
    state.sort_keys = std::move(keys);
    return state;
}

} // namespace wiola::gui
