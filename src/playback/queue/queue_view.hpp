/**
 * @file
 * @brief Live read-only observations of the playback queue.
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

#include <wiola/playback/queue_entry.hpp>

#include <cstddef>
#include <optional>
#include <span>

namespace wiola::engine {

/// Live queue observations on the owner's control thread; this port adds no synchronization.
/// The owner outlives its readers and keeps this object's address stable. Reacquire spans and
/// entry pointers after every mutation attempt, including a failed one. Copy an entry to retain
/// its facts. Revisions identify structural or metadata changes; selection is observed separately.
/// Entries have distinct valid identities in insertion order. A nonempty queue has one selected
/// position; current() points to that entry, and index_of() addresses the same tracks() span.
/// Structural revisions track changes to that span's membership/order, not shuffle policy.
class QueueView {
public:
    virtual ~QueueView() = default;
    QueueView(const QueueView&) = delete;
    QueueView& operator=(const QueueView&) = delete;
    QueueView(QueueView&&) = delete;
    QueueView& operator=(QueueView&&) = delete;

    [[nodiscard]] virtual std::span<const QueueEntry> tracks() const noexcept = 0;
    [[nodiscard]] virtual const QueueEntry* current() const noexcept = 0;
    [[nodiscard]] virtual std::optional<std::size_t> position() const noexcept = 0;
    [[nodiscard]] virtual std::optional<std::size_t> index_of(QueueEntryId id) const noexcept = 0;
    [[nodiscard]] virtual std::size_t structural_revision() const noexcept = 0;
    [[nodiscard]] virtual std::size_t metadata_revision() const noexcept = 0;

    [[nodiscard]] bool empty() const noexcept { return tracks().empty(); }

protected:
    QueueView() noexcept = default;
};

} // namespace wiola::engine
