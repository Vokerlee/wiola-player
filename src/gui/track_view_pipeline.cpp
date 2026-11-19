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

#include "track_view_pipeline.hpp"

#include <library/location.hpp>

namespace wiola::gui {

namespace {

bool matches(const engine::Track& track, const library::LibraryRepository* repository,
    const library::SearchQuery& query)
{
    const library::MediaId media = library::media_id_for(track.path());
    const library::TrackRecord* record = repository != nullptr ? repository->track(media) : nullptr;

    if (record != nullptr)
        return library::matches_search(query, *record);

    library::TrackRecord presented;
    presented.id = media;
    presented.embedded = track.metadata();
    return library::matches_search(query, presented);
}

} // namespace

bool track_query_active(const library::SearchQuery& query)
{
    return !query.title.empty() || !query.artist.empty() || !query.album.empty() ||
        !query.tags.empty() || !query.filters.empty();
}

std::vector<engine::Track> apply_track_view(const std::vector<engine::Track>& source,
    const library::LibraryRepository* repository, const library::SearchQuery& query,
    const std::vector<TrackSortKey>& sort_keys)
{
    std::vector<engine::Track> visible;
    visible.reserve(source.size());

    for (const engine::Track& track : source) {
        if (!track_query_active(query) || matches(track, repository, query))
            visible.push_back(track);
    }

    if (!sort_keys.empty() && repository != nullptr)
        return sorted_tracks(visible, *repository, sort_keys);

    return visible;
}

} // namespace wiola::gui
