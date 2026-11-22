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

bool contains(const std::string& value, const std::string& term)
{
    return term.empty() || value.find(term) != std::string::npos;
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

SearchFields search_fields_of(const TrackRecord& track)
{
    const auto resolved = resolve_metadata(track);
    SearchFields result{.title = normalized_tag_text(resolved.title),
        .artist = normalized_tag_text(resolved.artist),
        .album = normalized_tag_text(resolved.album),
        .tags = {}};
    for (const auto& tag : track.tags)
        result.tags.push_back({normalized_tag_text(tag.key), normalized_tag_text(tag.value)});
    return result;
}

bool matches_search_fields(const SearchQuery& query, const SearchFields& fields)
{
    if (!contains(fields.title, query.title) || !contains(fields.artist, query.artist) ||
        !contains(fields.album, query.album))
        return false;
    if (!query.tags.empty() && std::ranges::none_of(fields.tags, [&](const auto& tag) {
            return contains(tag.key, query.tags) || contains(tag.value, query.tags);
        }))
        return false;
    return std::ranges::all_of(query.filters, [&](const auto& filter) {
        const bool present = std::ranges::any_of(fields.tags, [&](const auto& tag) {
            return tag.key == filter.key && (filter.value.empty() || tag.value == filter.value);
        });
        return filter.mode == TagFilterMode::include ? present : !present;
    });
}

bool matches_normalized_search(const SearchQuery& query, const TrackRecord& track)
{
    return matches_search_fields(query, search_fields_of(track));
}

} // namespace wiola::library
