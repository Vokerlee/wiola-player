/**
 * @file
 * @brief Atomic, coalesced, backup-recoverable library persistence.
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

#include "library_json.hpp"
#include <library/in_memory_repository.hpp>

#include <QObject>
#include <QTimer>

#include <filesystem>
#include <functional>
#include <optional>

namespace wiola::persistence::qt {

class LibraryStorage final : public QObject {
public:
    explicit LibraryStorage(std::filesystem::path file, QObject* parent = nullptr);
    ~LibraryStorage() override;

    /// Loads the primary file, falling back to the last valid backup when necessary.
    LibraryJsonError load(library::LibrarySnapshot& output) const;

    /// Cancels any older scheduled snapshot and writes this one immediately and atomically.
    LibraryJsonError save(const library::LibrarySnapshot& snapshot);

    /// Coalesces snapshots produced in quick succession. The newest complete value wins.
    void schedule(library::LibrarySnapshot snapshot);

    /// Writes the scheduled value, retaining it for another attempt if writing fails.
    LibraryJsonError flush();

    /// Registers the observer for a scheduled write that could not be completed.
    void on_save_failed(std::function<void(LibraryJsonError)> observer);

    [[nodiscard]] bool pending() const noexcept { return pending_.has_value(); }

private:
    LibraryJsonError write(const library::LibrarySnapshot& snapshot);

    std::filesystem::path file_;
    std::filesystem::path backup_;
    std::optional<library::LibrarySnapshot> pending_;
    QTimer coalesce_;
    std::function<void(LibraryJsonError)> failure_observer_;
};

[[nodiscard]] LibraryJsonError restore_repository(const LibraryStorage& storage,
    library::InMemoryRepository& repository);

} // namespace wiola::persistence::qt
