/**
 * @file
 * @brief Owned inspector selection snapshots and explicit playing fallback.
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

#include "inspector_context.hpp"

#include <app/library/local_identity.hpp>
#include <qt/application/library/library_session.hpp>

#include <set>

namespace wiola::gui {

InspectorContext inspector_context(const app::LibrarySession& library,
    std::vector<SelectedTrack> selection, std::optional<TrackRow> playing)
{
    InspectorContext context;

    if (!selection.empty())
        context.origin = InspectorContext::Origin::selection;
    else if (playing) {
        context.origin = InspectorContext::Origin::playing;
        selection.push_back({playing->media, playing->id, playing->location});
    }

    std::set<library::MediaId> seen;

    for (const auto& item : selection) {
        if (!item.media.valid() || !seen.insert(item.media).second)
            continue;

        TrackRow row;
        bool registered = false;

        if (auto snapshot = library.snapshot(item.media)) {
            registered = true;
            row = present_record(item.occurrence, *snapshot);
            row.artwork = library.artwork(item.media);
            context.editable.push_back(std::move(*snapshot));
        } else {
            row.id = item.occurrence;
            row.media = item.media;

            const auto stem = item.location.stem().u8string();
            row.metadata.title.assign(stem.begin(), stem.end());
        }

        if (!item.location.empty())
            row.location = item.location;

        row.location_key = app::media_id_for(row.location).value;

        // Never display artwork belonging to a different playing track.
        if (playing && playing->media == row.media) {
            if (!registered)
                row.metadata = playing->metadata;
            if (!row.artwork.known() ||
                (row.artwork.availability() == media::ArtworkAvailability::failed &&
                    playing->artwork))
                row.artwork = playing->artwork;
        }

        context.tracks.push_back(std::move(row));
    }

    if (context.tracks.empty())
        context.origin = InspectorContext::Origin::none;

    return context;
}

} // namespace wiola::gui
