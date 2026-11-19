/**
 * @file
 * @brief Storage contract for tracks, tags, and saved playlists.
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

#include <library/records.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace wiola::library {

class LibraryRepository {
public:
    virtual ~LibraryRepository() = default;

    virtual bool put_track(TrackRecord track) = 0;
    [[nodiscard]] virtual const TrackRecord* track(const MediaId& id) const = 0;
    [[nodiscard]] virtual std::vector<TrackRecord> tracks() const = 0;

    virtual PlaylistId create_playlist(std::string name) = 0;
    virtual bool put_playlist(PlaylistRecord playlist) = 0;
    virtual bool rename_playlist(const PlaylistId& id, std::string name) = 0;
    [[nodiscard]] virtual const PlaylistRecord* playlist(const PlaylistId& id) const = 0;
    [[nodiscard]] virtual std::vector<PlaylistRecord> playlists() const = 0;
    virtual std::optional<PlaylistEntryId> append_track(const PlaylistId& playlist,
        const MediaId& media) = 0;
    virtual bool remove_entry(const PlaylistId& playlist, const PlaylistEntryId& entry) = 0;

    /// Adds one value. Exact duplicates and more than 256 tags on one track are rejected.
    virtual TagId add_tag(const MediaId& media, std::string key, std::string value) = 0;
    virtual bool remove_tag(const MediaId& media, const TagId& tag) = 0;
};

} // namespace wiola::library
