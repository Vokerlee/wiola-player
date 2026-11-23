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

#include <app/library/search_index.hpp>
#include <gui/model/tracks/track_view_pipeline.hpp>
#include <set>

#include <library/location.hpp>

namespace wiola::gui {

namespace {

bool matches(const TrackRow& track, const library::LibraryRepository* repository,
    const library::SearchQuery& query)
{
    const library::MediaId media = track.media;
    const library::TrackRecord* record = repository != nullptr ? repository->track(media) : nullptr;

    if (record != nullptr)
        return library::matches_normalized_search(query, *record);

    library::TrackRecord presented;
    presented.id = media;
    presented.embedded = track.metadata;
    presented.tags = track.tags;
    return library::matches_normalized_search(query, presented);
}

} // namespace

bool track_query_active(const library::SearchQuery& query)
{
    return !query.title.empty() || !query.artist.empty() || !query.album.empty() ||
        !query.tags.empty() || !query.filters.empty();
}

std::vector<TrackRow> apply_track_view(const std::vector<TrackRow>& source,
    const library::LibraryRepository* repository, const library::SearchQuery& query,
    const std::vector<TrackSortKey>& sort_keys, const app::SearchIndex* search,
    const std::map<library::MediaId, library::SearchFields>* fields, std::stop_token stop)
{
    std::vector<TrackRow> visible;
    visible.reserve(source.size());

    std::set<library::MediaId> matching;
    const bool active = track_query_active(query);
    if (search && active) {
        const auto& ids = search->query_normalized(query);
        matching.insert(ids.begin(), ids.end());
    }
    std::size_t visited = 0;
    for (const TrackRow& track : source) {
        if ((visited++ % 64) == 0 && stop.stop_requested())
            return {};
        const auto held = fields
            ? fields->find(track.media)
            : std::map<library::MediaId, library::SearchFields>::const_iterator{};
        const bool accepted = !active ||
            (fields && held != fields->end() ? library::matches_search_fields(query, held->second)
                                             : (search && search->contains(track.media)
                                                       ? matching.contains(track.media)
                                                       : matches(track, repository, query)));
        if (accepted)
            visible.push_back(track);
    }

    if (!sort_keys.empty())
        return sorted_rows(visible, repository, sort_keys, nullptr, stop);

    return visible;
}

} // namespace wiola::gui
