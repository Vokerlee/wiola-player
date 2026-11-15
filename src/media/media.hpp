/**
 * @file
 * @brief Dependency-light media values and typed operation results.
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

#include <media/result.hpp>
#include <pcm/stream_spec.hpp>
#include <utils/units.hpp>

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace wiola::media {

struct Artwork {
    std::vector<std::byte> data;
    std::string mime_type;

    [[nodiscard]] bool empty() const noexcept { return data.empty(); }
};

struct TrackMetadata {
    std::string title;
    std::string artist;
    std::string album;
    std::optional<units::Time> duration;
};

struct Metadata {
    TrackMetadata track;
    Artwork artwork;

    [[nodiscard]] bool empty() const noexcept
    {
        return track.title.empty() && track.artist.empty() && track.album.empty() &&
            artwork.empty() && !track.duration.has_value();
    }
};

struct FileFacts {
    std::filesystem::path path;
    std::string extension;
};

struct MediaInfo {
    pcm::StreamSpec spec;
    std::optional<units::Time> duration;
};

} // namespace wiola::media
