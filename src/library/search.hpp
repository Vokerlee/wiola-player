/**
 * @file
 * @brief Structured multi-field track search with normalized terms.
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

#include <library/records.hpp>

#include <string>

namespace wiola::library {

struct SearchQuery {
    std::string title;
    std::string artist;
    std::string album;
    std::string tags;
};

/// Trims and case-folds every field independently; empty fields do not restrict results.
[[nodiscard]] SearchQuery normalized_search_query(SearchQuery query);

/// Every non-empty field group must match; a tag term matches either a key or a value.
[[nodiscard]] bool matches_search(const SearchQuery& query, const TrackRecord& track);

} // namespace wiola::library
