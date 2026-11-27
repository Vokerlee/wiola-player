/**
 * @file
 * @brief Bounded metadata batches, coalescing, and queue-generation cancellation.
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

#include "metadata_coordinator.hpp"

#include <metadata/tag_reader.hpp>

#include <memory>
#include <stdexcept>
#include <utility>

namespace wiola::engine {

MetadataCoordinator::MetadataCoordinator(std::shared_ptr<media::MediaProvider> provider,
    std::size_t max_pending, std::size_t max_completed)
    : MetadataCoordinator{
          std::make_unique<TagReader>(std::move(provider), max_pending, max_completed)}
{
}

MetadataCoordinator::MetadataCoordinator(std::unique_ptr<MetadataWorker> worker)
    : reader_{std::move(worker)}
{
    if (!reader_)
        throw std::invalid_argument{"metadata worker must not be null"};
}

void MetadataCoordinator::replace(std::span<const QueueEntry> tracks)
{
    Groups groups{group(tracks)};

    ++generation_;
    reader_->cancel_before(generation_);

    deferred_.clear();
    next_submission_ = 0;

    start(groups);
    entries_ = std::move(groups);
}

void MetadataCoordinator::append(std::span<const QueueEntry> tracks)
{
    Groups additions{group(tracks)};

    for (auto& [resource, entries] : additions) {
        const auto existing{entries_.find(resource)};
        if (existing != entries_.end()) {
            existing->second.insert(existing->second.end(), entries.begin(), entries.end());
            entries.clear();
        }
    }

    start(additions);

    for (auto& [resource, entries] : additions) {
        if (!entries.empty())
            entries_.insert_or_assign(std::move(resource), std::move(entries));
    }
}

void MetadataCoordinator::cancel()
{
    ++generation_;
    reader_->cancel_before(generation_);

    entries_.clear();
    deferred_.clear();
    next_submission_ = 0;
}

std::vector<MetadataCoordinator::Reading> MetadataCoordinator::take()
{
    std::vector<Reading> readings;

    for (MetadataWorker::Reading& read : reader_->take()) {
        if (read.generation != generation_)
            continue;

        const auto entries{entries_.find(read.resource)};

        if (entries == entries_.end() || entries->second.front() != read.id)
            continue;

        readings.push_back(Reading{.entries = std::move(entries->second),
            .resource = std::move(read.resource),
            .result = std::move(read.result)});
        entries_.erase(entries);
    }

    submit();

    return readings;
}

bool MetadataCoordinator::busy() const noexcept
{
    return reader_->busy() || next_submission_ < deferred_.size();
}

MetadataCoordinator::Groups MetadataCoordinator::group(std::span<const QueueEntry> tracks) const
{
    Groups groups;

    for (const QueueEntry& track : tracks)
        groups[track.resource()].push_back(track.id());

    return groups;
}

void MetadataCoordinator::start(Groups& groups)
{
    deferred_.reserve(deferred_.size() + groups.size());

    for (const auto& [resource, entries] : groups) {
        if (!entries.empty())
            deferred_.push_back(MetadataWorker::Request{.generation = generation_,
                .id = entries.front(),
                .resource = resource});
    }

    submit();
}

void MetadataCoordinator::submit()
{
    if (next_submission_ == deferred_.size()) {
        deferred_.clear();
        next_submission_ = 0;

        return;
    }

    const std::span remaining{std::span{deferred_}.subspan(next_submission_)};
    next_submission_ += reader_->submit(remaining);

    if (next_submission_ == deferred_.size()) {
        deferred_.clear();
        next_submission_ = 0;
    }
}

} // namespace wiola::engine
