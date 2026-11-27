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

#include "library_changes.hpp"
#include "records.hpp"

namespace wiola::library {

/// Immutable owned after-values for a single accepted repository transaction.
enum class CommitPriority {
    ordinary,
    file_result,
};

struct RevisionDelta {
    CommitPriority priority{CommitPriority::ordinary};
    LibraryChangeBatch changes;
    std::vector<TrackRecord> tracks;
    std::vector<PlaylistRecord> playlists;
    std::vector<WorkspaceRecord> workspaces;
};

} // namespace wiola::library
