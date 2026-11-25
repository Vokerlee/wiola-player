/**
 * @file
 * @brief Application-owned synchronous snapshot writing contract.
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

#include <wiola/library/snapshot.hpp>

namespace wiola::app {

enum class SnapshotWriteError {
    none,
    io,
    invalid_snapshot,
    unsupported_version,
    too_large,
};

struct SnapshotWriteResult {
    SnapshotWriteError error{SnapshotWriteError::none};

    [[nodiscard]] bool succeeded() const noexcept { return error == SnapshotWriteError::none; }

    bool operator==(const SnapshotWriteResult&) const = default;
};

/// Synchronous durable writing. The driver owns encoding, file replacement and diagnostics.
/// The writer outlives the persistence workflow that calls it from its worker thread.
class SnapshotWriter {
public:
    virtual ~SnapshotWriter() = default;
    [[nodiscard]] virtual SnapshotWriteResult write(const library::LibrarySnapshot& snapshot) = 0;
};

} // namespace wiola::app
