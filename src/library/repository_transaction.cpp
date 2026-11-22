/**
 * @file
 * @brief Persistent track, metadata, tag, and playlist record shapes.
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

#include "repository_transaction.hpp"
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace wiola::library {

RepositoryTransaction::RepositoryTransaction(InMemoryRepository& repository, Persist persist)
    : repository_{repository}
    , checkpoint_{repository}
    , persist_{std::move(persist)}
{
    if (!persist_)
        throw std::invalid_argument{"a transaction requires an explicit persistence policy"};
}

RepositoryTransaction::~RepositoryTransaction()
{
    rollback();
}

bool RepositoryTransaction::commit()
{
    if (finished_)
        throw std::logic_error{"transaction already completed"};
    if (repository_.tracks_ == checkpoint_.tracks_ &&
        repository_.playlists_ == checkpoint_.playlists_ &&
        repository_.workspaces_ == checkpoint_.workspaces_) {
        finished_ = true;
        return true;
    }
    if (!persist_(repository_)) {
        rollback();
        return false;
    }
    finished_ = true;
    return true;
}

void RepositoryTransaction::rollback() noexcept
{
    static_assert(std::is_nothrow_move_assignable_v<InMemoryRepository>);
    if (finished_)
        return;
    repository_ = std::move(checkpoint_);
    finished_ = true;
}

} // namespace wiola::library
