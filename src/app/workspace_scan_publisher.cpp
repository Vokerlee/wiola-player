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

#include <QMetaObject>

namespace wiola::app {

WorkspaceScanPublisher::WorkspaceScanPublisher(QObject& gui_context, Observer observer)
    : state_{std::make_shared<State>(std::move(observer))}
    , context_{gui_context}
{
}

WorkspaceScanPublisher::~WorkspaceScanPublisher()
{
    cancel();
}

std::uint64_t WorkspaceScanPublisher::start(const library::WorkspaceRecord& workspace,
    std::vector<std::string> supported_extensions)
{
    const std::uint64_t generation = state_->generation.fetch_add(1) + 1;
    const std::weak_ptr<State> state = state_;
    scanner_.start(workspace, std::move(supported_extensions),
        [state, context = &context_, generation](WorkspaceScanBatch batch) {
            batch.generation = generation;
            receive(state, context, std::move(batch));
        });

    return generation;
}

void WorkspaceScanPublisher::cancel()
{
    state_->generation.fetch_add(1);
    scanner_.cancel();
}

void WorkspaceScanPublisher::receive(const std::weak_ptr<State>& weak, QObject* context,
    WorkspaceScanBatch batch)
{
    const auto state = weak.lock();
    if (!state || state->generation.load() != batch.generation)
        return;

    QMetaObject::invokeMethod(
        context,
        [weak, batch = std::move(batch)]() mutable {
            const auto current = weak.lock();
            if (current && current->generation.load() == batch.generation)
                current->observer(std::move(batch));
        },
        Qt::QueuedConnection
    );
}

} // namespace wiola::app
