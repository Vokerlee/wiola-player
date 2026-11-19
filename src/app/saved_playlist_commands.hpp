/**
 * @file
 * @brief Transactional create, rename, duplicate, and delete playlist commands.
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

#include <library/in_memory_repository.hpp>

#include <functional>
#include <string>

namespace wiola::app {

class SavedPlaylistCommands final {
public:
    using Persist = std::function<bool(const library::LibraryRepository&)>;

    SavedPlaylistCommands(library::InMemoryRepository& repository, Persist persist);

    [[nodiscard]] library::PlaylistId create(const std::string& name);
    [[nodiscard]] bool rename(const library::PlaylistId& id, const std::string& name);
    [[nodiscard]] library::PlaylistId duplicate(const library::PlaylistId& id);
    [[nodiscard]] bool remove(const library::PlaylistId& id);

private:
    struct Snapshot {
        std::vector<library::TrackRecord> tracks;
        std::vector<library::PlaylistRecord> playlists;
    };

    [[nodiscard]] Snapshot snapshot() const;
    [[nodiscard]] bool commit(const Snapshot& previous);

    library::InMemoryRepository& repository_;
    Persist persist_;
};

} // namespace wiola::app
