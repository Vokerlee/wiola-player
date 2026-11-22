/**
 * @file
 * @brief Coalesced library snapshots with explicit pending and retry state.
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
#include "library_store.hpp"
#include <QObject>
#include <QTimer>
#include <functional>
#include <optional>

namespace wiola::persistence::qt {

class LibrarySaveQueue final : public QObject {
public:
    explicit LibrarySaveQueue(LibraryStore& store, QObject* parent = nullptr);
    ~LibrarySaveQueue() override;
    /// A valid immediate snapshot supersedes older pending data, including on I/O failure.
    [[nodiscard]] LibraryStoreResult save(library::LibrarySnapshot snapshot);
    void schedule(library::LibrarySnapshot snapshot);
    [[nodiscard]] LibraryStoreResult flush();
    void on_save_failed(std::function<void(LibraryStoreResult)> observer);

    [[nodiscard]] bool pending() const noexcept { return pending_.has_value(); }

private:
    LibraryStore& store_;
    QTimer timer_;
    std::optional<library::LibrarySnapshot> pending_;
    std::function<void(LibraryStoreResult)> observer_;
};

} // namespace wiola::persistence::qt
