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

namespace {

template<typename T>
const T& resolve(const std::optional<T>& override, const T& embedded)
{
    return override ? *override : embedded;
}

} // namespace

std::string resolve_title(const TrackRecord& track)
{
    const auto& title = track.display_title();

    if (!track.overrides.title && title.empty() && !track.locations.empty())
        return track.locations.front().stem().string();

    return title;
}

ResolvedMetadata resolve_metadata(const TrackRecord& track)
{
    return {
        .title = resolve_title(track),
        .artist = track.display_artist(),
        .album = track.display_album(),
        .duration = track.embedded.duration,
        .genre = resolve(track.overrides.genre, track.embedded.genre),
        .year = resolve(track.overrides.year, track.embedded.year),
        .comment = resolve(track.overrides.comment, track.embedded.comment),
        .composer = resolve(track.overrides.composer, track.embedded.composer),
        .track_number = resolve(track.overrides.track_number, track.embedded.track_number),
        .disc_number = resolve(track.overrides.disc_number, track.embedded.disc_number),
        .tags = track.tags,
        .missing = track.missing,
    };
}

} // namespace wiola::library
