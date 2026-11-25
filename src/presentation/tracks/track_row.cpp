/**
 * @file
 * @brief Transport-independent track presentation values and explicit library resolution.
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

#include "track_row.hpp"
#include <library/location.hpp>
#include <library/metadata.hpp>

namespace wiola::gui {

TrackRow present_record(TrackRowId occurrence, const library::TrackRecord& record)
{
    auto values = library::resolve_metadata(record);
    TrackRow row;
    row.id = std::move(occurrence);
    row.media = record.id;
    row.location = library::preferred_location(record);
    row.location_key = library::media_id_for(row.location).value;
    row.metadata = {.title = std::move(values.title),
        .artist = std::move(values.artist),
        .album = std::move(values.album),
        .duration = values.duration,
        .genre = std::move(values.genre),
        .year = values.year,
        .comment = std::move(values.comment),
        .composer = std::move(values.composer),
        .track_number = values.track_number,
        .disc_number = values.disc_number};
    if (row.metadata.title.empty() && !record.overrides.title) {
        const auto filename = row.location.stem().u8string();
        row.metadata.title.assign(filename.begin(), filename.end());
    }
    row.tags = std::move(values.tags);
    row.missing = record.missing || row.location.empty();
    row.metadata_revision = record.metadata_revision;
    row.content_revision = record.content_revision;
    return row;
}

} // namespace wiola::gui
