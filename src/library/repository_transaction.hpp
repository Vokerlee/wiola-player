/**
 * @file
 * @brief Owner-thread repository transactions with complete rollback.
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
#include <functional>
#include <library/in_memory_repository.hpp>

namespace wiola::library {

/// Owner-thread transaction. Persistence sees a read-only candidate; failure restores all state,
/// including identity allocators. No library record pointer may be retained across rollback.
class RepositoryTransaction final {
public:
    using Persist = std::function<bool(const LibraryRepository&)>;
    RepositoryTransaction(InMemoryRepository& repository, Persist persist);
    ~RepositoryTransaction();
    RepositoryTransaction(const RepositoryTransaction&) = delete;
    RepositoryTransaction& operator=(const RepositoryTransaction&) = delete;
    RepositoryTransaction(RepositoryTransaction&&) = delete;
    RepositoryTransaction& operator=(RepositoryTransaction&&) = delete;
    [[nodiscard]] bool commit();
    void rollback() noexcept;

private:
    InMemoryRepository& repository_;
    InMemoryRepository::State checkpoint_;
    Persist persist_;
    bool finished_{false};
};

} // namespace wiola::library
