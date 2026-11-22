/**
 * @file
 * @brief Incrementally maintained normalized track search fields.
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

#include "search_index.hpp"

namespace wiola::app {

bool SearchIndex::upsert(const library::TrackRecord& track)
{
    if (!track.id.valid())
        return false;
    auto fields = library::search_fields_of(track);
    const auto found = entries_.find(track.id);
    if (found != entries_.end() && found->second == fields)
        return false;
    entries_.insert_or_assign(track.id, std::move(fields));
    cached_query_.reset();
    return true;
}

bool SearchIndex::remove(const library::MediaId& id)
{
    if (entries_.erase(id) == 0)
        return false;
    cached_query_.reset();
    return true;
}

void SearchIndex::clear()
{
    entries_.clear();
    cached_query_.reset();
    cached_matches_.clear();
}

std::vector<library::MediaId> SearchIndex::query(const library::SearchQuery& query) const
{
    return query_normalized(library::normalized_search_query(query));
}

const std::vector<library::MediaId>&
SearchIndex::query_normalized(const library::SearchQuery& query) const
{
    if (cached_query_ && *cached_query_ == query)
        return cached_matches_;
    cached_matches_.clear();
    for (const auto& [id, fields] : entries_)
        if (library::matches_search_fields(query, fields))
            cached_matches_.push_back(id);
    cached_query_ = query;
    return cached_matches_;
}

} // namespace wiola::app
