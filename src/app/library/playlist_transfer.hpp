/**
 * @file
 * @brief Transactional transfer of media occurrences into saved playlists.
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

#include "playlist_destination.hpp"

#include <wiola/library/in_memory_repository.hpp>
#include <wiola/library/repository_transaction.hpp>

#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <vector>

namespace wiola::app {

struct MediaTransfer {
    library::MediaId media;
    std::filesystem::path location;
};

struct PlaylistTransferResult {
    std::size_t added{0};
    library::CommitResult commit;

    [[nodiscard]] bool succeeded() const noexcept { return static_cast<bool>(commit); }
};

class PlaylistTransfer final {
public:
    using Persist = library::RepositoryTransaction::Persist;

    PlaylistTransfer(library::InMemoryRepository& repository,
        const PlaylistDestination& destination, Persist persist);

    [[nodiscard]] PlaylistTransferResult
    add_to_destination(const std::vector<MediaTransfer>& media);
    [[nodiscard]] PlaylistTransferResult add_to(const library::PlaylistId& playlist,
        const std::vector<MediaTransfer>& media);
    [[nodiscard]] PlaylistTransferResult move_to(const library::PlaylistId& source,
        const library::PlaylistId& destination, std::span<const library::PlaylistEntryId> entries);

private:
    [[nodiscard]] Persist persistence() const;

    library::InMemoryRepository& repository_;
    const PlaylistDestination& destination_;
    std::shared_ptr<Persist> persist_;
};

} // namespace wiola::app
