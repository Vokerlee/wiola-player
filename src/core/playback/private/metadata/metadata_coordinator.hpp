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

#pragma once

#include <metadata/metadata_worker.hpp>
#include <wiola/media/provider.hpp>
#include <wiola/playback/queue_entry.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <span>
#include <vector>

namespace wiola::engine {

/// Queue metadata applicability policy over the bounded worker.
class MetadataCoordinator {
public:
    struct Reading {
        std::vector<QueueEntryId> entries;
        std::filesystem::path path;
        media::Result<media::Metadata> result;
    };

    explicit MetadataCoordinator(std::shared_ptr<media::MediaProvider> provider,
        std::size_t max_pending = 256, std::size_t max_completed = 256);
    explicit MetadataCoordinator(std::unique_ptr<MetadataWorker> worker);

    MetadataCoordinator(const MetadataCoordinator&) = delete;
    MetadataCoordinator& operator=(const MetadataCoordinator&) = delete;
    MetadataCoordinator(MetadataCoordinator&&) = delete;
    MetadataCoordinator& operator=(MetadataCoordinator&&) = delete;

    /// Replaces the queue generation and cancels all older queued/completed work.
    void replace(std::span<const QueueEntry> tracks);

    /// Adds entries to the current generation, reading each new path only once.
    void append(std::span<const QueueEntry> tracks);
    void cancel();
    [[nodiscard]] std::vector<Reading> take();
    [[nodiscard]] bool busy() const noexcept;

private:
    using Entries = std::vector<QueueEntryId>;
    using Groups = std::map<std::filesystem::path, Entries>;

    Groups group(std::span<const QueueEntry> tracks) const;
    void start(Groups& groups);
    void submit();

    std::uint64_t generation_{0};
    Groups entries_;
    std::vector<MetadataWorker::Request> deferred_;
    std::size_t next_submission_{0};
    // Stop/join the owned worker before destroying the policy state it serves.
    std::unique_ptr<MetadataWorker> reader_;
};

} // namespace wiola::engine
