/**
 * @file
 * @brief Dependency-light media values and typed operation results.
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

#pragma once

#include <media/result.hpp>
#include <pcm/stream_spec.hpp>
#include <utils/units.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace wiola::media {

inline constexpr std::size_t max_encoded_artwork_bytes{8 * 1024 * 1024};

struct Artwork {
    std::vector<std::byte> data;
    std::string mime_type;
    std::uint64_t content_id{};

    [[nodiscard]] bool empty() const noexcept { return data.empty(); }
};

struct TrackMetadata {
    std::string title;
    std::string artist;
    std::string album;
    std::optional<units::Time> duration;
    std::string genre;
    std::optional<std::uint32_t> year;
    std::string comment;
    std::string composer;
    std::optional<std::uint32_t> track_number;
    std::optional<std::uint32_t> disc_number;

    bool operator==(const TrackMetadata&) const = default;
};

struct MetadataWriteCapabilities {
    bool title{false};
    bool artist{false};
    bool album{false};
    bool genre{false};
    bool year{false};
    bool comment{false};
    bool composer{false};
    bool track_number{false};
    bool disc_number{false};

    [[nodiscard]] bool any() const noexcept
    {
        return title || artist || album || genre || year || comment || composer || track_number ||
            disc_number;
    }
};

struct MetadataOverrides {
    std::optional<std::string> title;
    std::optional<std::string> artist;
    std::optional<std::string> album;
    std::optional<std::string> genre;
    std::optional<std::uint32_t> year;
    std::optional<std::string> comment;
    std::optional<std::string> composer;
    std::optional<std::uint32_t> track_number;
    std::optional<std::uint32_t> disc_number;

    [[nodiscard]] bool empty() const noexcept
    {
        return !title && !artist && !album && !genre && !year && !comment && !composer &&
            !track_number && !disc_number;
    }
};

struct ExpectedMediaState {
    std::optional<std::uint64_t> metadata_revision;
    std::optional<std::uint64_t> content_revision;
    std::string fingerprint;
};

struct MetadataWriteRequest {
    MetadataOverrides overrides;
    ExpectedMediaState expected;
};

enum class MetadataWriteState {
    written,
    unchanged,
    failed,
};

enum class MetadataWriteError {
    none,
    unsupported,
    conflicted,
    invalid_edit,
    io,
    cancelled,
    invalid_output,
};

struct MetadataWriteOutcome {
    MetadataWriteState state{MetadataWriteState::failed};
    MetadataWriteError error{MetadataWriteError::none};
    bool source_modified{false};
    std::string detail;
};

struct Metadata {
    TrackMetadata track;
    Artwork artwork;

    [[nodiscard]] bool empty() const noexcept
    {
        return track.title.empty() && track.artist.empty() && track.album.empty() &&
            track.genre.empty() && track.comment.empty() && track.composer.empty() &&
            artwork.empty() && !track.duration.has_value() && !track.year.has_value() &&
            !track.track_number.has_value() && !track.disc_number.has_value();
    }
};

struct FileFacts {
    std::filesystem::path path;
    std::string extension;
};

struct MediaInfo {
    pcm::StreamSpec spec;
    std::optional<units::Time> duration;
    bool seekable{false};
};

} // namespace wiola::media
