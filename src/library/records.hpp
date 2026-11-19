/**
 * @file
 * @brief Persistent track, metadata, tag, and playlist record shapes.
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

#include <library/identity.hpp>
#include <media/media.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace wiola::library {

/// Application-level values that take precedence without modifying embedded metadata.
struct StandardFieldOverrides {
    std::optional<std::string> title;
    std::optional<std::string> artist;
    std::optional<std::string> album;
};

struct TagRecord {
    TagId id;
    std::string key;
    std::string value;
};

struct TrackRecord {
    MediaId id;
    media::TrackMetadata embedded;
    StandardFieldOverrides overrides;
    std::vector<TagRecord> tags;
    std::vector<std::filesystem::path> locations;
    std::string fingerprint;
    std::uint64_t content_revision{0};
    std::uint64_t metadata_revision{0};
    bool missing{false};

    [[nodiscard]] const std::string& display_title() const noexcept
    {
        return overrides.title ? *overrides.title : embedded.title;
    }

    [[nodiscard]] const std::string& display_artist() const noexcept
    {
        return overrides.artist ? *overrides.artist : embedded.artist;
    }

    [[nodiscard]] const std::string& display_album() const noexcept
    {
        return overrides.album ? *overrides.album : embedded.album;
    }
};

struct PlaylistEntryRecord {
    PlaylistEntryId id;
    MediaId media;
};

struct PlaylistRecord {
    PlaylistId id;
    std::string name;
    std::vector<PlaylistEntryRecord> entries;
};

struct WorkspaceRecord {
    WorkspaceId id;
    std::filesystem::path root;
    bool recursive{true};
};

} // namespace wiola::library
