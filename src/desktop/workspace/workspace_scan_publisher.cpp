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

#include "workspace_scan_publisher.hpp"

namespace wiola::app {

WorkspaceScanPublisher::WorkspaceScanPublisher(QObject& gui_context, Observer observer)
    : delivery_{gui_context}
    , state_{std::make_shared<State>(std::move(observer))}
{
}

WorkspaceScanPublisher::~WorkspaceScanPublisher()
{
    stop();
}

void WorkspaceScanPublisher::stop()
{
    stopped_ = true;
    cancel();
    scanner_.stop();
}

std::uint64_t WorkspaceScanPublisher::start(const library::WorkspaceRecord& workspace,
    std::vector<std::string> supported_extensions, std::vector<library::TrackRecord> known)
{
    if (stopped_)
        return 0;
    const std::uint64_t generation = state_->generation.fetch_add(1) + 1;
    const std::weak_ptr<State> state = state_;
    scanner_.start(
        workspace, std::move(supported_extensions),
        [state, delivery = &delivery_, generation](WorkspaceScanBatch batch) {
            batch.generation = generation;
            receive(state, delivery, std::move(batch));
        },
        std::move(known)
    );

    return generation;
}

void WorkspaceScanPublisher::cancel()
{
    state_->generation.fetch_add(1);
    scanner_.cancel();
}

void WorkspaceScanPublisher::receive(const std::weak_ptr<State>& weak, QueuedDelivery* delivery,
    WorkspaceScanBatch batch)
{
    const auto state = weak.lock();
    if (!state || state->generation.load() != batch.generation)
        return;

    static_cast<void>(delivery->post([weak, batch = std::move(batch)]() mutable {
        const auto current = weak.lock();
        if (current && current->generation.load() == batch.generation)
            current->observer(std::move(batch));
    }));
}

} // namespace wiola::app
