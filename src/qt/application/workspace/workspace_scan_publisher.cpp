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
#include <QThread>
#include <algorithm>
#include <stdexcept>

namespace wiola::app {

WorkspaceScanPublisher::WorkspaceScanPublisher(QObject& context, Observer observer,
    WorkspaceScanScheduling policy)
    : delivery_{context}
    , lifetime_{std::make_shared<Lifetime>(Lifetime{this})}
    , observer_{std::move(observer)}
    , policy_{std::move(policy)}
{
    WorkspaceScanner validate{policy_.scan};
    if (!observer_ || policy_.max_active == 0 || policy_.max_active > 8 ||
        policy_.max_pending == 0 || policy_.max_pending > 64)
        throw std::invalid_argument{"invalid scan scheduling policy"};
}

WorkspaceScanPublisher::~WorkspaceScanPublisher()
{
    stop();
    lifetime_->owner = nullptr;
}

void WorkspaceScanPublisher::stop()
{
    stopped_ = true;
    pending_.clear();
    order_.clear();
    for (auto& [id, job] : running_)
        job.scanner->cancel();
    for (auto& [id, job] : running_)
        job.scanner->stop();
    running_.clear();
    generations_.clear();
}

std::uint64_t WorkspaceScanPublisher::start(const library::WorkspaceRecord& workspace,
    std::vector<std::string> extensions, std::vector<library::TrackRecord> known)
{
    Q_ASSERT(QThread::currentThread() == delivery_.thread());
    if (stopped_)
        return 0;
    const auto generation = ++generation_;
    generations_[workspace.id] = generation;
    if (const auto current = running_.find(workspace.id); current != running_.end())
        current->second.scanner->cancel();
    if (!pending_.contains(workspace.id) && running_.size() >= policy_.max_active &&
        pending_.size() >= policy_.max_pending) {
        const auto weak = std::weak_ptr<Lifetime>{lifetime_};
        static_cast<void>(delivery_.endpoint().post([weak, id = workspace.id, generation] {
            if (const auto state = weak.lock(); state && state->owner)
                state->owner->receive({.workspace = id,
                    .generation = generation,
                    .entries = {},
                    .status = WorkspaceScanStatus::failed,
                    .error = WorkspaceScanError::rejected,
                    .directories = {}});
        }));
        return generation;
    }
    if (!pending_.contains(workspace.id))
        order_.push_back(workspace.id);
    pending_.insert_or_assign(workspace.id,
        Request{workspace, std::move(extensions), std::move(known), generation});
    pump();
    return generation;
}

void WorkspaceScanPublisher::cancel(const library::WorkspaceId& id)
{
    generations_[id] = ++generation_;
    pending_.erase(id);
    std::erase(order_, id);
    if (const auto current = running_.find(id); current != running_.end())
        current->second.scanner->cancel();
}

void WorkspaceScanPublisher::cancel()
{
    for (auto& [id, generation] : generations_) {
        generation = ++generation_;
        if (const auto current = running_.find(id); current != running_.end())
            current->second.scanner->cancel();
    }
    pending_.clear();
    order_.clear();
}

void WorkspaceScanPublisher::pump()
{
    if (stopped_)
        return;
    for (std::size_t attempts = order_.size();
        attempts > 0 && !order_.empty() && running_.size() < policy_.max_active; --attempts) {
        auto id = std::move(order_.front());
        order_.pop_front();
        if (running_.contains(id)) {
            order_.push_back(std::move(id));
            continue;
        }
        auto found = pending_.find(id);
        if (found == pending_.end())
            continue;
        auto request = std::move(found->second);
        pending_.erase(found);
        auto scanner = std::make_unique<WorkspaceScanner>(policy_.scan);
        auto* worker = scanner.get();
        running_.emplace(id, Running{request.generation, std::move(scanner)});
        const auto weak = std::weak_ptr<Lifetime>{lifetime_};
        const auto endpoint = delivery_.endpoint();
        try {
            worker->start(
                request.workspace, std::move(request.extensions),
                [weak, endpoint, generation = request.generation](WorkspaceScanBatch batch) {
                    batch.generation = generation;
                    static_cast<void>(endpoint.post([weak, batch = std::move(batch)]() mutable {
                        if (const auto state = weak.lock(); state && state->owner)
                            state->owner->receive(std::move(batch));
                    }));
                },
                std::move(request.known)
            );
        } catch (const std::runtime_error&) {
            receive({.workspace = id,
                .generation = request.generation,
                .entries = {},
                .status = WorkspaceScanStatus::failed,
                .error = WorkspaceScanError::io,
                .directories = {}});
        }
    }
}

void WorkspaceScanPublisher::receive(WorkspaceScanBatch batch)
{
    if (stopped_)
        return;
    const bool finished = batch.status != WorkspaceScanStatus::running;
    if (finished) {
        const auto current = running_.find(batch.workspace);
        if (current != running_.end() && current->second.generation == batch.generation)
            running_.erase(current);
    }
    if (const auto latest = generations_.find(batch.workspace);
        latest != generations_.end() && latest->second == batch.generation)
        observer_(std::move(batch));
    if (finished)
        pump();
}

} // namespace wiola::app
