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

#include <engine/load/metadata_coordinator.hpp>

#include <utility>

namespace wiola::engine {

MetadataCoordinator::MetadataCoordinator(std::shared_ptr<media::MediaProvider> provider,
    std::size_t max_pending, std::size_t max_completed)
    : reader_{std::move(provider), max_pending, max_completed}
{
}

MetadataCoordinator::MetadataCoordinator(MetadataReader reader, std::size_t max_pending,
    std::size_t max_completed)
    : reader_{std::move(reader), max_pending, max_completed}
{
}

void MetadataCoordinator::replace(std::span<const Track> tracks)
{
    Groups groups{group(tracks)};
    ++generation_;
    reader_.cancel_before(generation_);

    deferred_.clear();
    next_submission_ = 0;

    start(groups);
    entries_ = std::move(groups);
}

void MetadataCoordinator::append(std::span<const Track> tracks)
{
    Groups additions{group(tracks)};

    for (auto& [path, entries] : additions) {
        const auto existing{entries_.find(path)};
        if (existing != entries_.end()) {
            existing->second.insert(existing->second.end(), entries.begin(), entries.end());
            entries.clear();
        }
    }

    start(additions);

    for (auto& [path, entries] : additions) {
        if (!entries.empty())
            entries_.insert_or_assign(std::move(path), std::move(entries));
    }
}

void MetadataCoordinator::cancel()
{
    ++generation_;
    reader_.cancel_before(generation_);

    entries_.clear();
    deferred_.clear();
    next_submission_ = 0;
}

std::vector<MetadataCoordinator::Reading> MetadataCoordinator::take()
{
    std::vector<Reading> readings;

    for (TagReader::Reading& read : reader_.take()) {
        const auto entries{entries_.find(read.path)};

        if (entries == entries_.end())
            continue;

        readings.push_back(Reading{.entries = std::move(entries->second),
            .path = std::move(read.path),
            .tags = std::move(read.tags),
            .failed = read.failed});
        entries_.erase(entries);
    }

    submit();

    return readings;
}

bool MetadataCoordinator::busy() const noexcept
{
    return reader_.busy() || next_submission_ < deferred_.size();
}

MetadataCoordinator::Groups MetadataCoordinator::group(std::span<const Track> tracks) const
{
    Groups groups;

    for (const Track& track : tracks)
        groups[track.path()].push_back(track.id());

    return groups;
}

void MetadataCoordinator::start(Groups& groups)
{
    deferred_.reserve(deferred_.size() + groups.size());

    for (const auto& [path, entries] : groups) {
        if (!entries.empty())
            deferred_.push_back(
                TagReader::Request{.generation = generation_, .id = entries.front(), .path = path}
            );
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
    next_submission_ += reader_.start(remaining);

    if (next_submission_ == deferred_.size()) {
        deferred_.clear();
        next_submission_ = 0;
    }
}

} // namespace wiola::engine
