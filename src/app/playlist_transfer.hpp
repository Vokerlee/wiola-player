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

#include "active_playlist.hpp"

#include <library/in_memory_repository.hpp>

#include <filesystem>
#include <functional>
#include <span>
#include <vector>

namespace wiola::app {

struct MediaTransfer {
    library::MediaId media;
    std::filesystem::path location;
};

struct PlaylistTransferResult {
    std::size_t added{0};
    bool succeeded{false};
};

class PlaylistTransfer final {
public:
    using Persist = std::function<bool(const library::LibraryRepository&)>;

    PlaylistTransfer(library::InMemoryRepository& repository, const ActivePlaylist& active,
        Persist persist);

    [[nodiscard]] PlaylistTransferResult add_to_active(const std::vector<MediaTransfer>& media);
    [[nodiscard]] PlaylistTransferResult add_to(const library::PlaylistId& playlist,
        const std::vector<MediaTransfer>& media);
    [[nodiscard]] PlaylistTransferResult move_to(const library::PlaylistId& source,
        const library::PlaylistId& destination, std::span<const library::PlaylistEntryId> entries);

private:
    library::InMemoryRepository& repository_;
    const ActivePlaylist& active_;
    Persist persist_;
};

} // namespace wiola::app
