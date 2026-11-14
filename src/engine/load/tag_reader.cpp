/**
 * @file
 * @brief Bounded, generation-aware background tag reading.
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

#include "tag_reader.hpp"

#include <algorithm>
#include <new>
#include <stdexcept>
#include <utility>

namespace wiola::engine {

TagReader::TagReader()
    : TagReader{codec::read_tags}
{
}

TagReader::TagReader(MetadataReader read, std::size_t max_pending, std::size_t max_completed)
    : reader_{std::move(read)}
    , max_pending_{max_pending}
    , max_completed_{max_completed}
{
    if (max_pending_ == 0 || max_completed_ == 0)
        throw std::invalid_argument{"metadata capacities must be positive"};
}

TagReader::~TagReader()
{
    if (thread_.joinable()) {
        thread_.request_stop();
        waiting_.notify_all();
        collected_.notify_all();
        thread_.join();
    }
}

void TagReader::start(std::vector<Request> requests)
{
    if (requests.empty())
        return;

    {
        const std::lock_guard asked{mutex_};

        if (requests.size() > max_pending_ - std::min(asked_.size(), max_pending_))
            throw std::length_error{"metadata pending capacity exceeded"};

        for (Request& request : requests)
            asked_.push_back(std::move(request));
    }

    if (!thread_.joinable())
        thread_ = std::jthread{[this](const std::stop_token& stop) { work(stop); }};

    waiting_.notify_one();
}

void TagReader::cancel_before(std::uint64_t generation)
{
    {
        const std::lock_guard asked{mutex_};
        latest_generation_ = std::max(latest_generation_, generation);
        std::erase_if(asked_,
            [generation](const Request& request) { return request.generation < generation; });
        std::erase_if(read_,
            [generation](const Reading& reading) { return reading.generation < generation; });
    }

    collected_.notify_all();
}

std::vector<TagReader::Reading> TagReader::take()
{
    std::vector<Reading> readings;
    {
        const std::lock_guard asked{mutex_};
        readings = std::exchange(read_, {});
    }
    collected_.notify_one();
    return readings;
}

bool TagReader::busy() const noexcept
{
    const std::lock_guard asked{mutex_};
    return !asked_.empty() || reading_;
}

void TagReader::work(const std::stop_token& stop)
{
    while (true) {
        Request request;

        {
            std::unique_lock asked{mutex_};
            waiting_.wait(asked, stop, [this] { return !asked_.empty(); });

            if (stop.stop_requested())
                return;

            request = std::move(asked_.front());
            asked_.pop_front();
            reading_ = true;
        }

        codec::Tags tags;
        bool failed{false};

        try {
            tags = reader_(request.path);
        } catch (const std::bad_alloc&) {
            // Allocation failure is fatal: no reliable error object or queue storage can be made.
            throw;
        } catch (...) {
            failed = true;
        }

        {
            std::unique_lock asked{mutex_};
            reading_ = false;

            if (request.generation < latest_generation_)
                continue;

            collected_.wait(asked, stop, [this] { return read_.size() < max_completed_; });

            if (stop.stop_requested())
                return;

            read_.push_back(Reading{.generation = request.generation,
                .id = request.id,
                .path = std::move(request.path),
                .tags = std::move(tags),
                .failed = failed});
        }
    }
}

} // namespace wiola::engine
