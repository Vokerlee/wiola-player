/**
 * @file
 * @brief Explicit resolution of embedded, overridden, and user-owned metadata layers.
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

#include "metadata.hpp"

namespace wiola::library {

ResolvedMetadata resolve_metadata(const TrackRecord& track)
{
    ResolvedMetadata resolved{
        .title = track.display_title(),
        .artist = track.display_artist(),
        .album = track.display_album(),
        .duration = track.embedded.duration,
        .genre = track.embedded.genre,
        .year = track.embedded.year,
        .comment = track.embedded.comment,
        .composer = track.embedded.composer,
        .track_number = track.embedded.track_number,
        .disc_number = track.embedded.disc_number,
        .tags = track.tags,
        .missing = track.missing,
    };

    if (!track.overrides.title && resolved.title.empty() && !track.locations.empty())
        resolved.title = track.locations.front().stem().string();

    return resolved;
}

} // namespace wiola::library
