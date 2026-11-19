/**
 * @file
 * @brief End-to-end persistent storage for repository-backed saved playlists.
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

#include "saved_playlist_storage.hpp"

#include <utility>

namespace wiola::gui {

SavedPlaylistStorage::SavedPlaylistStorage(std::filesystem::path file)
    : storage_{std::move(file)}
{
}

LibraryJsonError SavedPlaylistStorage::save(const library::LibraryRepository& repository)
{
    return storage_.save(LibrarySnapshot{
        .tracks = repository.tracks(),
        .playlists = repository.playlists(),
        .workspaces = repository.workspaces(),
    });
}

LibraryJsonError SavedPlaylistStorage::restore(library::InMemoryRepository& repository) const
{
    LibrarySnapshot snapshot;
    const LibraryJsonError error = storage_.load(snapshot);

    if (error != LibraryJsonError::none)
        return error;

    return repository.replace_all(snapshot.tracks, snapshot.playlists, snapshot.workspaces)
        ? LibraryJsonError::none
        : LibraryJsonError::invalid_record;
}

} // namespace wiola::gui
