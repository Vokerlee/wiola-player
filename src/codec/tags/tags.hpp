/**
 * @file
 * @brief Common track metadata and artwork read from a file.
 * @author Roman Glaz
 * @copyright © 2026, <vokerlee@gmail.com>
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

#pragma once

#include <media/media.hpp>

#include <filesystem>

namespace wiola::codec {

/// Unverified file claims, with absent duration represented explicitly rather than as zero.
struct Tags {
    media::TrackMetadata track;
    media::Artwork artwork;

    [[nodiscard]] bool empty() const noexcept
    {
        return track.title.empty() && track.artist.empty() && track.album.empty() &&
            artwork.empty() && !track.duration.has_value();
    }
};

/// Reads file claims without decoding the stream; unavailable fields remain absent.
[[nodiscard]] Tags read_tags(const std::filesystem::path& path);

} // namespace wiola::codec
