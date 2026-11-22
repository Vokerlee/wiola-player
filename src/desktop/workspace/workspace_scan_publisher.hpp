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

#include <desktop/queued_delivery.hpp>

#include <atomic>
#include <memory>

namespace wiola::app {

class WorkspaceScanPublisher final {
public:
    using Observer = WorkspaceScanner::Observer;

    WorkspaceScanPublisher(QObject& gui_context, Observer observer);
    ~WorkspaceScanPublisher();

    std::uint64_t start(const library::WorkspaceRecord& workspace,
        std::vector<std::string> supported_extensions,
        std::vector<library::TrackRecord> known = {});
    void cancel();
    void stop();

private:
    struct State {
        explicit State(Observer observer)
            : observer{std::move(observer)}
        {
        }

        std::atomic<std::uint64_t> generation{0};
        Observer observer;
    };

    static void receive(const std::weak_ptr<State>& state, QueuedDelivery::Endpoint delivery,
        WorkspaceScanBatch batch);

    QueuedDelivery delivery_;
    std::shared_ptr<State> state_;
    WorkspaceScanner scanner_;
    bool stopped_{false};
};

} // namespace wiola::app
