/**
 * @file
 * @brief Cooperative worker-thread audio enumeration for folder workspaces.
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
#include <filesystem>
#include <functional>
#include <thread>
#include <vector>

namespace wiola::app {

enum class WorkspaceScanStatus {
    running,
    complete,
    cancelled,
    failed,
};

struct WorkspaceScanBatch {
    library::WorkspaceId workspace;
    std::uint64_t generation{0};
    std::vector<WorkspaceScanEntry> entries;
    WorkspaceScanStatus status{WorkspaceScanStatus::running};
};

class WorkspaceScanner final {
public:
    using Observer = std::function<void(WorkspaceScanBatch)>;

    WorkspaceScanner() = default;
    ~WorkspaceScanner();

    std::uint64_t start(const library::WorkspaceRecord& workspace,
        std::vector<std::string> supported_extensions, Observer observer,
        std::vector<library::TrackRecord> known = {});
    void cancel();
    void stop();

private:
    void scan(std::stop_token stopped, const library::WorkspaceRecord& workspace,
        std::vector<std::string> supported_extensions, const Observer& observer,
        std::uint64_t generation, const std::vector<library::TrackRecord>& known) const;

    std::jthread worker_;
    std::stop_source stopped_;
    std::uint64_t generation_{0};
    bool stopped_permanently_{false};
};

} // namespace wiola::app
