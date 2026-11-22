/**
 * @file
 * @brief Committed library change batches and scoped observers.
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
#include <cstdint>
#include <functional>
#include <library/identity.hpp>
#include <memory>
#include <vector>

namespace wiola::library {

enum class MediaChangeFields : unsigned {
    none = 0,
    embedded = 1,
    overrides = 2,
    tags = 4,
    locations = 8,
    availability = 16,
    content = 32,
    revisions = 64,
    all = 127,
};

constexpr MediaChangeFields operator|(MediaChangeFields first, MediaChangeFields second) noexcept
{
    return static_cast<MediaChangeFields>(static_cast<unsigned>(first) |
        static_cast<unsigned>(second));
}

struct MediaChange {
    MediaId media;
    std::uint64_t metadata_revision{0};
    std::uint64_t content_revision{0};
    MediaChangeFields fields{MediaChangeFields::none};

    [[nodiscard]] bool includes(MediaChangeFields value) const noexcept
    {
        return (static_cast<unsigned>(fields) & static_cast<unsigned>(value)) != 0;
    }
};

/// One committed owner-thread mutation, not a speculative edit or a promise of file-tag writing.
struct LibraryChangeBatch {
    std::uint64_t revision{0};
    std::vector<MediaChange> media;
    std::vector<MediaId> removed_media;
    std::vector<PlaylistId> playlists;
    std::vector<PlaylistId> removed_playlists;
    std::vector<WorkspaceId> workspaces;
    std::vector<WorkspaceId> removed_workspaces;
    [[nodiscard]] bool empty() const noexcept;
};

/// Scoped owner-thread observers. Nested publications are delivered in revision order.
class LibraryChanges final {
    struct State;

public:
    using Observer = std::function<void(const LibraryChangeBatch&)>;

    class Subscription final {
    public:
        Subscription() = default;
        ~Subscription();
        Subscription(Subscription&& other) noexcept;
        Subscription& operator=(Subscription&& other) noexcept;
        Subscription(const Subscription&) = delete;
        Subscription& operator=(const Subscription&) = delete;
        void reset() noexcept;

    private:
        friend class LibraryChanges;
        Subscription(std::weak_ptr<State> state, std::uint64_t id);
        std::weak_ptr<State> state_;
        std::uint64_t id_{0};
    };

    LibraryChanges();
    LibraryChanges(const LibraryChanges&) = delete;
    LibraryChanges& operator=(const LibraryChanges&) = delete;
    [[nodiscard]] Subscription observe(Observer observer);

private:
    friend class RepositoryTransaction;
    void check_owner() const;
    // Observer exceptions cannot roll back committed data; notify other receivers before
    // rethrowing.
    void publish(LibraryChangeBatch batch);
    std::shared_ptr<State> state_;
};

} // namespace wiola::library
