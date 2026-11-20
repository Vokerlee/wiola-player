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

#include "search.hpp"

#include "metadata.hpp"
#include "tags.hpp"

#include <algorithm>

namespace wiola::library {

namespace {

bool contains(std::string value, const std::string& term)
{
    return term.empty() || normalized_tag_text(std::move(value)).find(term) != std::string::npos;
}

bool matches_tag(const TagRecord& tag, const std::string& term)
{
    return contains(tag.key, term) || contains(tag.value, term);
}

} // namespace

SearchQuery normalized_search_query(SearchQuery query)
{
    query.title = normalized_tag_text(query.title);
    query.artist = normalized_tag_text(query.artist);
    query.album = normalized_tag_text(query.album);
    query.tags = normalized_tag_text(query.tags);

    return query;
}

bool matches_search(const SearchQuery& query, const TrackRecord& track)
{
    const SearchQuery normalized = normalized_search_query(query);
    const ResolvedMetadata metadata = resolve_metadata(track);

    if (!contains(metadata.title, normalized.title) ||
        !contains(metadata.artist, normalized.artist) ||
        !contains(metadata.album, normalized.album))
        return false;

    return normalized.tags.empty() || std::ranges::any_of(track.tags, [&](const TagRecord& tag) {
        return matches_tag(tag, normalized.tags);
    });
}

} // namespace wiola::library
