/**
 * @file
 * @brief Convenient edits to the canonical browser state in behavior tests.
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

#include <wiola/presentation/tracks/track_view_state.hpp>

namespace wiola::testing {

template<typename View>
void set_track_query(View& view, library::SearchQuery query)
{
    auto state = view.view_state();
    state.query = std::move(query);
    view.set_view_state(std::move(state));
}

template<typename View>
void set_track_sort(View& view, std::vector<gui::TrackSortKey> keys)
{
    auto state = view.view_state();
    state.sort_keys = std::move(keys);
    view.set_view_state(std::move(state));
}

} // namespace wiola::testing
