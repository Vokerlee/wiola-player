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

#include "tag_reader.hpp"

#include <utility>

namespace wiola::engine {

TagReader::TagReader()
    : TagReader{codec::read_tags}
{
}

TagReader::TagReader(MetadataReader read)
    : reader_{std::move(read)}
{
}

TagReader::~TagReader()
{
    if (thread_.joinable()) {
        thread_.request_stop();
        waiting_.notify_all();
        thread_.join();
    }
}

void TagReader::start(std::vector<Request> requests)
{
    if (requests.empty())
        return;

    {
        const std::lock_guard asked{mutex_};
        for (Request& request : requests)
            asked_.push_back(std::move(request));
    }

    if (!thread_.joinable())
        thread_ = std::jthread{[this](const std::stop_token& stop) { work(stop); }};

    waiting_.notify_one();
}

std::vector<TagReader::Reading> TagReader::take()
{
    const std::lock_guard asked{mutex_};
    return std::exchange(read_, {});
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

        codec::Tags tags{reader_(request.path)};

        {
            const std::lock_guard asked{mutex_};
            reading_ = false;
            read_.push_back(
                Reading{.id = request.id, .path = std::move(request.path), .tags = std::move(tags)}
            );
        }
    }
}

} // namespace wiola::engine
