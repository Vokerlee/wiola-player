/**
 * @file
 * @brief Bounded metadata batches, coalescing, and queue-generation cancellation.
 * @author Roman Glaz
 * @copyright © 2026, <vokerlee@gmail.com>
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

#include <engine/load/tag_reader.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <span>
#include <vector>

namespace wiola::engine {

/// Application-side metadata policy over the bounded tag worker.
class MetadataCoordinator {
public:
    struct Reading {
        std::vector<QueueEntryId> entries;
        std::filesystem::path path;
        codec::Tags tags;
        bool failed{false};
    };

    explicit MetadataCoordinator(MetadataReader reader, std::size_t max_pending = 256,
        std::size_t max_completed = 256);

    NO_COPY_SEMANTIC(MetadataCoordinator);
    NO_MOVE_SEMANTIC(MetadataCoordinator);

    /// Replaces the queue generation and cancels all older queued/completed work.
    void replace(std::span<const Track> tracks);

    /// Adds entries to the current generation, reading each new path only once.
    void append(std::span<const Track> tracks);
    void cancel();
    [[nodiscard]] std::vector<Reading> take();
    [[nodiscard]] bool busy() const noexcept;

private:
    using Entries = std::vector<QueueEntryId>;
    using Groups = std::map<std::filesystem::path, Entries>;

    Groups group(std::span<const Track> tracks) const;
    void start(Groups& groups);

    TagReader reader_;
    std::uint64_t generation_{0};
    Groups entries_;
};

} // namespace wiola::engine
