/**
 * @file
 * @brief Controlled collaborators for portable persistence tests.
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

#include <allocation_failure.hpp>
#include <wiola/app/library/library_persistence.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

namespace wiola::app::test::persistence {

class ManualDelivery final {
public:
    app::LibraryPersistence::Delivery callbacks()
    {
        return {
            [this](std::function<void()> task) {
                std::lock_guard lock{mutex_};

                pending_.push_back(std::move(task));
                changed_.notify_all();

                return true;
            },
            [this] { drain(); },
        };
    }

    bool wait_pending(std::size_t count)
    {
        std::unique_lock lock{mutex_};
        return changed_.wait_for(lock, std::chrono::seconds{5},
            [&] { return pending_.size() >= count; });
    }

    void reverse_pending()
    {
        std::lock_guard lock{mutex_};
        std::reverse(pending_.begin(), pending_.end());
    }

    void drain()
    {
        for (;;) {
            std::function<void()> task;
            {
                std::lock_guard lock{mutex_};

                if (pending_.empty())
                    return;

                task = std::move(pending_.front());
                pending_.pop_front();
            }
            task();
        }
    }

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<std::function<void()>> pending_;
};

#if WIOLA_TEST_ALLOCATOR_REPLACEMENT
inline app::LibraryPersistence::Delivery fail_after_first_delivery(ManualDelivery& delivery)
{
    auto callbacks = delivery.callbacks();
    callbacks.post = [post = std::move(callbacks.post), first = true](auto task) mutable {
        const bool accepted = post(std::move(task));

        if (std::exchange(first, false)) {
            // Arm on the worker after the seed write and its completion have been published.
            // One failure permits reporting the error and retrying on this same worker.
            static thread_local std::optional<wiola::testing::AllocationFailure> failure;
            failure.emplace(0, wiola::testing::AllocationFailure::Mode::once);
        }

        return accepted;
    };

    return callbacks;
}
#endif

class ControlledWriter final : public app::SnapshotWriter {
public:
    app::SnapshotWriteResult write(const library::LibrarySnapshot& snapshot) override
    {
        std::unique_lock lock{mutex_};

        snapshots_.push_back(snapshot);
        entered_ = true;
        changed_.notify_all();
        changed_.wait(lock, [this] { return !blocked_; });

        if (failure_)
            std::rethrow_exception(failure_);

        return result_;
    }

    void block()
    {
        std::lock_guard lock{mutex_};

        blocked_ = true;
        entered_ = false;
    }

    void release(app::SnapshotWriteResult result = {}, std::exception_ptr failure = {})
    {
        std::lock_guard lock{mutex_};

        result_ = result;
        failure_ = std::move(failure);
        blocked_ = false;
        changed_.notify_all();
    }

    bool wait_entered()
    {
        std::unique_lock lock{mutex_};
        return changed_.wait_for(lock, std::chrono::seconds{5}, [this] { return entered_; });
    }

    library::LibrarySnapshot last_snapshot() const
    {
        std::lock_guard lock{mutex_};
        return snapshots_.back();
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::vector<library::LibrarySnapshot> snapshots_;
    app::SnapshotWriteResult result_;
    std::exception_ptr failure_;
    bool blocked_{}, entered_{};
};

inline std::shared_ptr<library::RevisionDelta> revision(std::uint64_t number,
    const char* id = "media")
{
    auto delta = std::make_shared<library::RevisionDelta>();
    delta->changes.revision = number;

    library::TrackRecord track;
    track.id = {id};
    track.embedded.title = id;
    delta->tracks.push_back(std::move(track));

    return delta;
}

class LibraryPersistenceCore : public ::testing::Test {
protected:
    ~LibraryPersistenceCore() override
    {
        store.release(); // Unblock before joining even when a test assertion exits early.
        writer.reset();
    }

    void start(library::LibrarySnapshot initial = {},
        app::LibraryPersistence::Observer observer = {}, app::LibraryPersistenceLimits limits = {})
    {
        writer.reset();
        writer = std::make_unique<app::LibraryPersistence>(store, delivery.callbacks(),
            std::move(initial), std::move(observer), limits);
    }

    ManualDelivery delivery;
    ControlledWriter store;
    std::unique_ptr<app::LibraryPersistence> writer;
};

} // namespace wiola::app::test::persistence
