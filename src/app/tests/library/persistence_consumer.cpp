/**
 * @file
 * @brief Portable persistence target consumer with a supplied snapshot driver.
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

#include <wiola/app/library/library_persistence.hpp>

#include <deque>
#include <mutex>

class Writer final : public wiola::app::SnapshotWriter {
public:
    wiola::app::SnapshotWriteResult write(const wiola::library::LibrarySnapshot& value) override
    {
        snapshot = value;
        return {};
    }

    wiola::library::LibrarySnapshot snapshot;
};

int main()
{
    std::mutex mutex;
    std::deque<std::function<void()>> pending;
    wiola::app::LibraryPersistence::Delivery delivery{
        [&](std::function<void()> task) {
            std::lock_guard lock{mutex};

            pending.push_back(std::move(task));

            return true;
        },
        [&] {
            std::deque<std::function<void()>> tasks;
            {
                std::lock_guard lock{mutex};
                tasks.swap(pending);
            }

            for (auto& task : tasks)
                task();
        },
    };
    Writer output;
    wiola::app::LibraryPersistence persistence{output, std::move(delivery), {}};
    auto delta = std::make_shared<wiola::library::RevisionDelta>();
    delta->changes.revision = 1;

    wiola::library::TrackRecord track;
    track.id = {"opaque-media"};
    delta->tracks.push_back(track);

    const auto accepted = persistence.accept(delta);

    if (!accepted || accepted.durable() || !persistence.flush().succeeded())
        return 1;

    if (!accepted.receipt->durable() || output.snapshot.tracks.size() != 1 ||
        output.snapshot.tracks.front().id != track.id)
        return 2;

    return 0;
}
