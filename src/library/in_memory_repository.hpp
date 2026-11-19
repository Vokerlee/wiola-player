/**
 * @file
 * @brief Deterministic in-memory library storage.
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

#include <library/repository.hpp>

#include <map>

namespace wiola::library {

class InMemoryRepository final : public LibraryRepository {
public:
    bool put_track(TrackRecord track) override;
    [[nodiscard]] const TrackRecord* track(const MediaId& id) const override;
    [[nodiscard]] std::vector<TrackRecord> tracks() const override;

    /// Validates a complete snapshot before replacing any current records.
    bool replace_all(const std::vector<TrackRecord>& tracks,
        const std::vector<PlaylistRecord>& playlists);

    PlaylistId create_playlist(std::string name) override;
    bool put_playlist(PlaylistRecord playlist) override;
    bool rename_playlist(const PlaylistId& id, std::string name) override;
    bool delete_playlist(const PlaylistId& id) override;
    [[nodiscard]] const PlaylistRecord* playlist(const PlaylistId& id) const override;
    [[nodiscard]] std::vector<PlaylistRecord> playlists() const override;
    std::optional<PlaylistEntryId> append_track(const PlaylistId& playlist,
        const MediaId& media) override;
    bool remove_entry(const PlaylistId& playlist, const PlaylistEntryId& entry) override;

    TagId add_tag(const MediaId& media, std::string key, std::string value) override;
    bool remove_tag(const MediaId& media, const TagId& tag) override;

private:
    PlaylistId next_playlist_id();
    PlaylistEntryId next_entry_id();
    TagId next_tag_id();

    [[nodiscard]] bool entry_exists(const PlaylistEntryId& id) const;
    [[nodiscard]] bool tag_exists(const TagId& id) const;

    std::map<MediaId, TrackRecord> tracks_;
    std::map<PlaylistId, PlaylistRecord> playlists_;
    std::size_t next_playlist_{1};
    std::size_t next_entry_{1};
    std::size_t next_tag_{1};
};

} // namespace wiola::library
