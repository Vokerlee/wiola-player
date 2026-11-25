/**
 * @file
 * @brief Atomic reconciliation of workspace files with persistent media records.
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

#include "workspace_discovery.hpp"
#include <wiola/library/repository_transaction.hpp>

#include <span>

namespace wiola::app {

struct WorkspaceReconciliationResult {
    std::size_t learned{0};
    std::size_t relocated{0};
    std::size_t restored{0};
    std::size_t edited{0};
    std::size_t missing{0};
    std::size_t outside{0};
    std::size_t failed{0};
    bool succeeded{false};
};

class WorkspaceReconciliation final {
public:
    explicit WorkspaceReconciliation(library::RepositoryTransaction::Persist persist)
        : persist_{std::move(persist)}
    {
    }

    [[nodiscard]] WorkspaceReconciliationResult reconcile(library::InMemoryRepository& repository,
        const library::WorkspaceId& workspace, std::span<const WorkspaceScanEntry> entries) const;

private:
    library::RepositoryTransaction::Persist persist_;
};

} // namespace wiola::app
