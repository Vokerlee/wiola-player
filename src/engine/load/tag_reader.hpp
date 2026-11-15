/**
 * @file
 * @brief Reads what queued files say about themselves, off the thread that queued them.
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

#include <codec/tags/tags.hpp>
#include <core/macros.hpp>
#include <engine/queue/track.hpp>

#include <condition_variable>
#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace wiola::engine {

using MetadataReader = std::function<codec::Tags(const std::filesystem::path&)>;

/// Background tag reads retain queue identity independently of paths and row positions.
class TagReader {
public:
    struct Request {
        QueueEntryId id;
        std::filesystem::path path;
    };

    struct Reading {
        QueueEntryId id;
        std::filesystem::path path;
        codec::Tags tags;
    };

    TagReader();
    explicit TagReader(MetadataReader read);

    NO_COPY_SEMANTIC(TagReader);
    NO_MOVE_SEMANTIC(TagReader);

    ~TagReader();

    void start(std::vector<Request> requests);
    [[nodiscard]] std::vector<Reading> take();
    [[nodiscard]] bool busy() const noexcept;

private:
    void work(const std::stop_token& stop);

    MetadataReader reader_;
    mutable std::mutex mutex_;
    std::condition_variable_any waiting_;
    std::deque<Request> asked_;
    std::vector<Reading> read_;
    bool reading_{false};
    std::jthread thread_;
};

} // namespace wiola::engine
