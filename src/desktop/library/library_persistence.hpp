/**
 * @file
 * @brief Synchronized payload delivery to the desktop event loop.
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
#include <condition_variable>
#include <deque>
#include <desktop/queued_delivery.hpp>
#include <library/commit_receipt.hpp>
#include <library/revision_delta.hpp>
#include <map>
#include <mutex>
#include <qt/storage/library_store.hpp>
#include <thread>

namespace wiola::app {

/// One bounded writer; only owned immutable deltas cross the GUI/worker boundary.
class LibraryPersistence final {
public:
    using Result = persistence::qt::LibraryStoreResult;
    using Observer = std::function<void(std::uint64_t, Result)>;
    LibraryPersistence(QObject& owner, persistence::qt::LibraryStore& store,
        library::LibrarySnapshot initial, Observer observer = {});
    ~LibraryPersistence();
    [[nodiscard]] library::CommitResult accept(std::shared_ptr<const library::RevisionDelta> delta);
    void when_durable(std::uint64_t revision, std::function<void(bool)> observer);
    void retry();
    [[nodiscard]] Result flush();
    void stop();
    [[nodiscard]] std::uint64_t pending_revision() const;
    [[nodiscard]] std::uint64_t durable_revision() const;

private:
    struct Lifetime {
        LibraryPersistence* owner;
    };

    struct Awaiter {
        std::uint64_t revision;
        std::function<void(bool)> observer;
    };

    void run(library::LibrarySnapshot initial);
    void completed(std::uint64_t revision, Result result);
    static std::size_t retained_bytes(const library::RevisionDelta& delta);
    QueuedDelivery delivery_;
    std::shared_ptr<Lifetime> lifetime_;
    persistence::qt::LibraryStore& store_;
    Observer observer_;
    std::map<std::uint64_t, std::shared_ptr<library::CommitReceipt>> receipts_;
    std::vector<Awaiter> awaiters_;
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<std::shared_ptr<const library::RevisionDelta>> pending_;
    std::size_t pending_bytes_{};
    std::uint64_t latest_{}, durable_{}, attempted_{}, attempt_{};
    Result last_result_;
    bool active_{}, retry_{}, stopping_{}, has_durable_{};
    std::jthread worker_;
};

} // namespace wiola::app
