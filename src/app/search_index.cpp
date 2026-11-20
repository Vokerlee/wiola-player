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

#include <library/tags.hpp>

#include <algorithm>

namespace wiola::app {

namespace {

void normalize(std::optional<std::string>& value)
{
    if (value)
        *value = library::normalized_tag_text(*value);
}

library::TrackRecord normalized_fields(const library::TrackRecord& track)
{
    library::TrackRecord normalized;
    normalized.id = track.id;
    normalized.embedded.title = library::normalized_tag_text(track.embedded.title);
    normalized.embedded.artist = library::normalized_tag_text(track.embedded.artist);
    normalized.embedded.album = library::normalized_tag_text(track.embedded.album);
    normalized.overrides = track.overrides;

    normalize(normalized.overrides.title);
    normalize(normalized.overrides.artist);
    normalize(normalized.overrides.album);

    normalized.tags.reserve(track.tags.size());

    for (const library::TagRecord& tag : track.tags) {
        normalized.tags.push_back({.id = tag.id,
            .key = library::normalized_tag_text(tag.key),
            .value = library::normalized_tag_text(tag.value)});
    }

    return normalized;
}

bool same_fields(const library::TrackRecord& first, const library::TrackRecord& second)
{
    return first.embedded.title == second.embedded.title &&
        first.embedded.artist == second.embedded.artist &&
        first.embedded.album == second.embedded.album &&
        first.overrides.title == second.overrides.title &&
        first.overrides.artist == second.overrides.artist &&
        first.overrides.album == second.overrides.album && first.tags == second.tags;
}

} // namespace

bool SearchIndex::upsert(const library::TrackRecord& track)
{
    if (!track.id.valid())
        return false;

    library::TrackRecord normalized = normalized_fields(track);
    const auto existing = std::ranges::find(entries_, track.id, &Entry::media);

    if (existing != entries_.end()) {
        if (same_fields(existing->normalized, normalized))
            return false;

        existing->normalized = std::move(normalized);
        return true;
    }

    entries_.push_back({.media = track.id, .normalized = std::move(normalized)});
    return true;
}

bool SearchIndex::remove(const library::MediaId& media)
{
    return std::erase_if(entries_, [&media](const Entry& entry) { return entry.media == media; }) !=
        0;
}

std::vector<library::MediaId> SearchIndex::query(const library::SearchQuery& query) const
{
    const library::SearchQuery normalized = library::normalized_search_query(query);
    std::vector<library::MediaId> matches;

    for (const Entry& entry : entries_) {
        if (library::matches_search(normalized, entry.normalized))
            matches.push_back(entry.media);
    }

    return matches;
}

} // namespace wiola::app
