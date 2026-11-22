/**
 * @file
 * @brief Serializable library snapshots and repository conversion.
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

namespace wiola::library {

struct LibrarySnapshot {
    std::vector<TrackRecord> tracks;
    std::vector<PlaylistRecord> playlists;
    std::vector<WorkspaceRecord> workspaces;
};

[[nodiscard]] inline LibrarySnapshot snapshot_of(const LibraryRepository& repository)
{
    return {.tracks = repository.tracks(),
        .playlists = repository.playlists(),
        .workspaces = repository.workspaces()};
}

} // namespace wiola::library
