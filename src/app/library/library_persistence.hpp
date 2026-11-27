/**
 * @file
 * @brief Portable bounded snapshot persistence and owner-thread acknowledgments.
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

#include <wiola/app/library/snapshot_writer.hpp>
#include <wiola/library/commit_receipt.hpp>
#include <wiola/library/revision_delta.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

namespace wiola::app {

/// Queue capacities, including one revision/receipt slot reserved for physical file results.
struct LibraryPersistenceLimits {
    std::size_t pending_revisions{64};
    std::size_t pending_payload_bytes{32 * 1024 * 1024};
    std::size_t file_result_extra_bytes{16 * 1024 * 1024};
    std::size_t outstanding_receipts{4096};
};

/// One bounded writer; only owned immutable deltas cross the GUI/worker boundary.
class LibraryPersistence final {
public:
    using Result = SnapshotWriteResult;

    /// post is producer-safe and queues a task for the accepting thread; drain runs queued
    /// tasks there. The supplied writer and delivery receiver outlive this workflow.
    struct Delivery {
        std::function<bool(std::function<void()>)> post;
        std::function<void()> drain;
    };

    using Observer = std::function<void(std::uint64_t, Result)>;
    LibraryPersistence(SnapshotWriter& store, Delivery delivery, library::LibrarySnapshot initial,
        Observer observer = {}, LibraryPersistenceLimits limits = {});
    ~LibraryPersistence();
    LibraryPersistence(const LibraryPersistence&) = delete;
    LibraryPersistence& operator=(const LibraryPersistence&) = delete;
    LibraryPersistence(LibraryPersistence&&) = delete;
    LibraryPersistence& operator=(LibraryPersistence&&) = delete;
    [[nodiscard]] library::CommitResult accept(std::shared_ptr<const library::RevisionDelta> delta);
    void when_durable(std::uint64_t revision, std::function<void(bool)> observer);
    void retry();
    [[nodiscard]] Result flush();
    void stop();
    [[nodiscard]] std::uint64_t pending_revision() const;
    [[nodiscard]] std::uint64_t durable_revision() const;

private:
    struct State;
    const std::shared_ptr<State> state_;
};

} // namespace wiola::app
