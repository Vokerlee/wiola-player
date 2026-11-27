/**
 * @file
 * @brief Owned workspace scan observations shared across policy and frontend adapters.
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

#include "workspace_discovery.hpp"

#include <cstdint>
#include <vector>

namespace wiola::app {

enum class WorkspaceScanStatus {
    running,
    complete,
    cancelled,
    failed,
};

enum class WorkspaceScanError {
    none,
    unreadable_root,
    permission_denied,
    limit_exceeded,
    io,
    rejected,
    unsupported_formats,
};

struct WorkspaceScanBatch {
    library::WorkspaceId workspace;
    std::uint64_t generation{0};
    std::vector<WorkspaceScanEntry> entries;
    WorkspaceScanStatus status{WorkspaceScanStatus::running};
    WorkspaceScanError error{WorkspaceScanError::none};
    std::vector<std::filesystem::path> directories;
};

} // namespace wiola::app
