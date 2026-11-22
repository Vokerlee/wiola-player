/**
 * @file
 * @brief Explicit playback-queue conversion at the Now Playing presentation boundary.
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

#include "queue_rows.hpp"
#include <library/location.hpp>

namespace wiola::gui {

TrackRow queue_row(const engine::Track& track)
{
    TrackRow row;
    row.id = track.id();
    row.media = library::media_id_for(track.path());
    row.location = track.path();
    row.location_key = row.media.value;
    row.metadata = track.metadata();
    row.artwork = track.artwork();
    row.artwork_revision = track.revision();
    row.metadata_revision = track.revision();
    return row;
}

std::vector<TrackRow> queue_rows(std::span<const engine::Track> tracks)
{
    std::vector<TrackRow> rows;
    rows.reserve(tracks.size());
    for (const auto& track : tracks)
        rows.push_back(queue_row(track));
    return rows;
}

std::vector<engine::QueueEntryId> queue_entry_ids(std::span<const TrackRowId> rows)
{
    std::vector<engine::QueueEntryId> result;
    result.reserve(rows.size());
    for (const auto& row : rows)
        if (const auto* id = std::get_if<engine::QueueEntryId>(&row))
            result.push_back(*id);
    return result;
}

std::optional<engine::QueueEntryId> queue_entry_id(const std::optional<TrackRowId>& row)
{
    if (row)
        if (const auto* id = std::get_if<engine::QueueEntryId>(&*row))
            return *id;
    return {};
}

} // namespace wiola::gui
