/**
 * @file
 * @brief Owner-thread repository transactions with complete rollback.
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
#include "commit_receipt.hpp"
#include <array>
#include <functional>
#include <library/in_memory_repository.hpp>

namespace wiola::library {

/// Owner-thread transaction. Persistence sees a read-only candidate; failure restores all state,
/// including identity allocators. No library record pointer may be retained across rollback.
class RepositoryTransaction final {
public:
    using Persist = std::function<CommitResult(const LibraryRepository&)>;
    RepositoryTransaction(InMemoryRepository& repository, Persist persist,
        CommitPriority priority = CommitPriority::ordinary);
    ~RepositoryTransaction();
    RepositoryTransaction(const RepositoryTransaction&) = delete;
    RepositoryTransaction& operator=(const RepositoryTransaction&) = delete;
    RepositoryTransaction(RepositoryTransaction&&) = delete;
    RepositoryTransaction& operator=(RepositoryTransaction&&) = delete;
    [[nodiscard]] CommitResult commit();

private:
    enum class Phase {
        editing,
        committing,
        finished,
    };
    void check_mutation() const;
    void restore() noexcept;
    InMemoryRepository& repository_;
    friend class InMemoryRepository;
    void touch_track(const MediaId& id);
    void touch_playlist(const PlaylistId& id);
    void touch_workspace(const WorkspaceId& id);
    using Tracks = decltype(InMemoryRepository::State::tracks_);
    using Playlists = decltype(InMemoryRepository::State::playlists_);
    using Workspaces = decltype(InMemoryRepository::State::workspaces_);
    std::map<MediaId, Tracks::node_type> tracks_;
    std::map<PlaylistId, Playlists::node_type> playlists_;
    std::map<WorkspaceId, Workspaces::node_type> workspaces_;
    std::array<std::size_t, 4> allocators_;
    Persist persist_;
    CommitPriority priority_;
    Phase phase_{Phase::editing};
};

} // namespace wiola::library
