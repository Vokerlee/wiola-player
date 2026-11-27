/**
 * @file
 * @brief Generation-aware queued publication of workspace scan batches.
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

#include <app/workspace/workspace_scanner.hpp>
#include <qt/queued_delivery.hpp>

#include <deque>
#include <map>
#include <memory>

namespace wiola::app {

struct WorkspaceScanScheduling {
    std::size_t max_active{2}, max_pending{8};
    WorkspaceScanPolicy scan;
};

class WorkspaceScanPublisher final {
public:
    using Observer = WorkspaceScanner::Observer;
    WorkspaceScanPublisher(QObject& context, Observer observer,
        WorkspaceScanScheduling policy = {});
    ~WorkspaceScanPublisher();
    std::uint64_t start(const library::WorkspaceRecord& workspace,
        std::vector<std::string> extensions, std::vector<library::TrackRecord> known = {});
    void cancel(const library::WorkspaceId& workspace);
    void cancel();
    void stop();

private:
    struct Lifetime {
        WorkspaceScanPublisher* owner;
    };

    struct Request {
        library::WorkspaceRecord workspace;
        std::vector<std::string> extensions;
        std::vector<library::TrackRecord> known;
        std::uint64_t generation;
    };

    struct Running {
        std::uint64_t generation;
        std::unique_ptr<WorkspaceScanner> scanner;
    };

    void pump();
    void receive(WorkspaceScanBatch batch);
    QueuedDelivery delivery_;
    std::shared_ptr<Lifetime> lifetime_;
    Observer observer_;
    WorkspaceScanScheduling policy_;
    std::map<library::WorkspaceId, std::uint64_t> generations_;
    std::map<library::WorkspaceId, Running> running_;
    std::map<library::WorkspaceId, Request> pending_;
    std::deque<library::WorkspaceId> order_;
    std::uint64_t generation_{};
    bool stopped_{};
};

} // namespace wiola::app
