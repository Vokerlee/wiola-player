/**
 * @file
 * @brief Catalog relocation policy over observed content facts.
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

#include "relocation.hpp"

#include <algorithm>
#include <utility>

namespace wiola::library {

std::optional<MediaId> match_relocation(std::span<const TrackRecord> tracks,
    std::string_view fingerprint)
{
    if (fingerprint.empty())
        return std::nullopt;

    const TrackRecord* match{nullptr};

    for (const TrackRecord& track : tracks) {
        if (track.fingerprint != fingerprint)
            continue;

        if (match != nullptr)
            return std::nullopt;

        match = &track;
    }

    return match ? std::optional{match->id} : std::nullopt;
}

std::optional<Relocation> reconcile_location(TrackRecord& track,
    const std::filesystem::path& location, std::string fingerprint)
{
    if (!track.id.valid() || fingerprint.empty())
        return std::nullopt;
    RelocationKind kind{RelocationKind::unchanged};

    if (track.fingerprint.empty()) {
        kind = RelocationKind::learned;
    } else if (track.fingerprint != fingerprint) {
        kind = RelocationKind::edited;
        ++track.content_revision;
    }

    track.fingerprint = std::move(fingerprint);
    track.missing = false;

    if (std::ranges::find(track.locations, location) == track.locations.end())
        track.locations.push_back(location);

    return Relocation{.media = track.id, .location = location, .kind = kind};
}

} // namespace wiola::library
