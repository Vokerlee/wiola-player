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

#include <metadata/metadata_worker.hpp>
#include <wiola/media/provider.hpp>

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stop_token>
#include <thread>
#include <vector>

namespace wiola::engine {

/// Bounded tag worker. Submission accepts as much of a batch as capacity permits.
class TagReader final : public MetadataWorker {
public:
    /// The provider or reader is selected by the host, never by the worker.
    explicit TagReader(std::shared_ptr<media::MediaProvider> provider,
        std::size_t max_pending = 256, std::size_t max_completed = 256);
    explicit TagReader(MetadataReader read, std::size_t max_pending = 256,
        std::size_t max_completed = 256,
        std::size_t max_completed_artwork_bytes = 64 * 1024 * 1024);
    explicit TagReader(ContextMetadataReader read, std::size_t max_pending = 256,
        std::size_t max_completed = 256,
        std::size_t max_completed_artwork_bytes = 64 * 1024 * 1024);

    TagReader(const TagReader&) = delete;
    TagReader& operator=(const TagReader&) = delete;
    TagReader(TagReader&&) = delete;
    TagReader& operator=(TagReader&&) = delete;

    ~TagReader() override;

    /// Accepts a prefix of `requests` and returns its size. A full queue accepts none.
    std::size_t submit(std::span<Request> requests) override;

    std::size_t submit(std::vector<Request>&& requests) { return submit(std::span{requests}); }

    void cancel_before(std::uint64_t generation) override;
    [[nodiscard]] std::vector<Reading> take() override;
    [[nodiscard]] bool busy() const noexcept override;

private:
    void work(const std::stop_token& stop);

    ContextMetadataReader reader_;
    std::stop_source active_stop_{std::nostopstate};
    const std::size_t max_pending_;
    const std::size_t max_completed_;
    const std::size_t max_completed_artwork_bytes_;
    mutable std::mutex mutex_;
    std::condition_variable_any waiting_;
    std::condition_variable_any collected_;
    std::deque<Request> asked_;
    std::vector<Reading> read_;
    std::size_t completed_artwork_bytes_{0};
    std::uint64_t latest_generation_{0};
    std::optional<std::uint64_t> active_generation_;
    std::jthread thread_;
};

} // namespace wiola::engine
