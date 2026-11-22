/**
 * @file
 * @brief Scheduling and retry behavior for coalesced library saves.
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

#include "library_save_queue.hpp"
#include <utility>

namespace wiola::persistence::qt {

LibrarySaveQueue::LibrarySaveQueue(LibraryStore& store, QObject* parent)
    : QObject{parent}
    , store_{store}
{
    timer_.setSingleShot(true);
    timer_.setInterval(50);
    connect(&timer_, &QTimer::timeout, this, [this] {
        const auto result = flush();
        if (!result.succeeded() && observer_)
            observer_(result);
    });
}

LibrarySaveQueue::~LibrarySaveQueue()
{
    static_cast<void>(flush());
}

LibraryStoreResult LibrarySaveQueue::save(library::LibrarySnapshot snapshot)
{
    QByteArray bytes;
    const auto valid = write_library_json(snapshot, bytes);
    if (valid != LibraryJsonError::none)
        return {.schema = valid};
    timer_.stop();
    pending_ = std::move(snapshot);
    return flush();
}

void LibrarySaveQueue::schedule(library::LibrarySnapshot snapshot)
{
    pending_ = std::move(snapshot);
    timer_.start();
}

LibraryStoreResult LibrarySaveQueue::flush()
{
    timer_.stop();
    if (!pending_)
        return {};
    const auto result = store_.save(*pending_);
    if (result.succeeded())
        pending_.reset();
    return result;
}

void LibrarySaveQueue::on_save_failed(std::function<void(LibraryStoreResult)> observer)
{
    observer_ = std::move(observer);
}

} // namespace wiola::persistence::qt
