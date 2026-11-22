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

#include <library/library_changes.hpp>
#include <library/repository.hpp>

#include <map>
#include <utility>

namespace wiola::library {

class InMemoryRepository final : public LibraryRepository {
public:
    InMemoryRepository() = default;

    /// A candidate copy has its own observer scope; copying data does not copy view subscriptions.
    InMemoryRepository(const InMemoryRepository& other)
        : data_{other.data_}
    {
    }

    InMemoryRepository& operator=(const InMemoryRepository&) = delete;

    [[nodiscard]] LibraryChanges::Subscription observe(LibraryChanges::Observer observer)
    {
        return changes_.observe(std::move(observer));
    }

    [[nodiscard]] std::uint64_t change_revision() const noexcept { return change_revision_; }

    bool put_track(TrackRecord track) override;
    [[nodiscard]] const TrackRecord* track(const MediaId& id) const override;
    [[nodiscard]] std::vector<TrackRecord> tracks() const override;

    /// Validates a complete snapshot before replacing any current records.
    bool replace_all(const std::vector<TrackRecord>& tracks,
        const std::vector<PlaylistRecord>& playlists,
        const std::vector<WorkspaceRecord>& workspaces);

    PlaylistId create_playlist(std::string name) override;
    bool put_playlist(PlaylistRecord playlist) override;
    bool rename_playlist(const PlaylistId& id, std::string name) override;
    bool delete_playlist(const PlaylistId& id) override;
    [[nodiscard]] const PlaylistRecord* playlist(const PlaylistId& id) const override;
    [[nodiscard]] std::vector<PlaylistRecord> playlists() const override;
    std::optional<PlaylistEntryId> append_track(const PlaylistId& playlist,
        const MediaId& media) override;
    bool remove_entry(const PlaylistId& playlist, const PlaylistEntryId& entry) override;

    WorkspaceId create_workspace(std::filesystem::path root, bool recursive = true) override;
    bool put_workspace(WorkspaceRecord workspace) override;
    [[nodiscard]] const WorkspaceRecord* workspace(const WorkspaceId& id) const override;
    [[nodiscard]] std::vector<WorkspaceRecord> workspaces() const override;
    bool delete_workspace(const WorkspaceId& id) override;

    TagId add_tag(const MediaId& media, std::string key, std::string value) override;
    bool remove_tag(const MediaId& media, const TagId& tag) override;

private:
    friend class RepositoryTransaction;
    PlaylistId next_playlist_id();
    PlaylistEntryId next_entry_id();
    WorkspaceId next_workspace_id();
    TagId next_tag_id();

    [[nodiscard]] bool entry_exists(const PlaylistEntryId& id) const;
    [[nodiscard]] bool tag_exists(const TagId& id) const;

    struct State {
        std::map<MediaId, TrackRecord> tracks_;
        std::map<PlaylistId, PlaylistRecord> playlists_;
        std::map<WorkspaceId, WorkspaceRecord> workspaces_;
        std::size_t next_playlist_{1};
        std::size_t next_entry_{1};
        std::size_t next_workspace_{1};
        std::size_t next_tag_{1};
    };

    State data_;
    LibraryChanges changes_;
    std::uint64_t change_revision_{0};
    bool transaction_active_{false};
};

} // namespace wiola::library
