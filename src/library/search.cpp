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

bool present(const TrackRecord& track, const TagFilter& filter)
{
    return std::ranges::any_of(track.tags, [&](const TagRecord& tag) {
        return normalized_tag_text(tag.key) == filter.key &&
            (filter.value.empty() || normalized_tag_text(tag.value) == filter.value);
    });
}

bool matches_filters(const TrackRecord& track, const std::vector<TagFilter>& filters)
{
    return std::ranges::all_of(filters, [&](const TagFilter& filter) {
        return filter.mode == TagFilterMode::include ? present(track, filter)
                                                     : !present(track, filter);
    });
}

} // namespace

SearchQuery normalized_search_query(SearchQuery query)
{
    query.title = normalized_tag_text(query.title);
    query.artist = normalized_tag_text(query.artist);
    query.album = normalized_tag_text(query.album);
    query.tags = normalized_tag_text(query.tags);

    for (TagFilter& filter : query.filters) {
        filter.key = normalized_tag_text(filter.key);
        filter.value = normalized_tag_text(filter.value);
    }

    return query;
}

bool matches_search(const SearchQuery& query, const TrackRecord& track)
{
    return matches_normalized_search(normalized_search_query(query), track);
}

bool matches_normalized_search(const SearchQuery& normalized, const TrackRecord& track)
{
    const ResolvedMetadata metadata = resolve_metadata(track);

    if (!contains(metadata.title, normalized.title) ||
        !contains(metadata.artist, normalized.artist) ||
        !contains(metadata.album, normalized.album))
        return false;

    if (!normalized.tags.empty() && std::ranges::none_of(track.tags, [&](const TagRecord& tag) {
            return matches_tag(tag, normalized.tags);
        }))
        return false;

    return matches_filters(track, normalized.filters);
}

} // namespace wiola::library
