/**
 * @file
 * @brief One presentation pipeline for filtering and sorting track rows.
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

#include <presentation/tracks/track_sorter.hpp>

#include <library/search.hpp>
#include <map>
#include <stop_token>

namespace wiola::app {

class SearchIndex;

}

namespace wiola::gui {

[[nodiscard]] bool track_query_active(const library::SearchQuery& query);

[[nodiscard]] std::vector<TrackRow> apply_track_view(const std::vector<TrackRow>& source,
    const library::LibraryRepository* repository, const library::SearchQuery& query,
    const std::vector<TrackSortKey>& sort_keys, const app::SearchIndex* search = nullptr,
    const std::map<library::MediaId, library::SearchFields>* fields = nullptr,
    std::stop_token stop = {});

} // namespace wiola::gui
