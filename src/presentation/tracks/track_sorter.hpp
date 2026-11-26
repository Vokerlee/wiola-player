/**
 * @file
 * @brief Stable multi-column track ordering with deterministic fallbacks.
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

#include <wiola/presentation/tracks/track_sort.hpp>

#include <stop_token>
#include <vector>

namespace wiola::gui {

struct TrackSortStatistics {
    std::size_t prepared_values{}, comparisons{};
};

[[nodiscard]] std::vector<TrackRow> sorted_rows(const std::vector<TrackRow>& tracks,
    const library::LibraryRepository* repository, const std::vector<TrackSortKey>& keys,
    TrackSortStatistics* statistics = nullptr, std::stop_token stop = {});

} // namespace wiola::gui
