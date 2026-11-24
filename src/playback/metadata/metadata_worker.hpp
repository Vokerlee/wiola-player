/**
 * @file
 * @brief Bounded, generation-aware background tag reading.
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

#include <wiola/media/metadata.hpp>
#include <wiola/media/result.hpp>
#include <wiola/playback/queue_entry_id.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace wiola::engine {

/// Consumer-owned bounded metadata work. Delivery owns its values; policy accepts current identity.
/// Submission/cancellation/collection have one lifecycle caller; busy supports concurrent queries.
class MetadataWorker {
public:
    struct Request {
        std::uint64_t generation{0};
        QueueEntryId id;
        std::filesystem::path path;
    };

    /// Echo the accepted request identity; payload ownership transfers with delivery.
    struct Reading {
        std::uint64_t generation{0};
        QueueEntryId id;
        std::filesystem::path path;
        media::Result<media::Metadata> result;
    };

    MetadataWorker(const MetadataWorker&) = delete;
    MetadataWorker& operator=(const MetadataWorker&) = delete;
    MetadataWorker(MetadataWorker&&) = delete;
    MetadataWorker& operator=(MetadataWorker&&) = delete;
    virtual ~MetadataWorker() = default;

    /// Move an accepted prefix; leave unaccepted requests available to the caller.
    virtual std::size_t submit(std::span<Request> requests) = 0;
    /// Cooperatively retire older work; callers still validate returned generation/identity.
    virtual void cancel_before(std::uint64_t generation) = 0;
    [[nodiscard]] virtual std::vector<Reading> take() = 0;
    /// Queued/active work; available completed results alone do not make a worker busy.
    [[nodiscard]] virtual bool busy() const noexcept = 0;

protected:
    MetadataWorker() = default;
};

} // namespace wiola::engine
