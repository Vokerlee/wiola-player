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

#pragma once
#include <presentation/tracks/track_sort.hpp>
#include <wiola/library/search.hpp>

namespace wiola::gui {

struct TrackViewState {
    library::SearchQuery query;
    std::vector<TrackSortKey> sort_keys;
    bool operator==(const TrackViewState&) const = default;
};

[[nodiscard]] TrackViewState normalized_track_view_state(TrackViewState state);

} // namespace wiola::gui
