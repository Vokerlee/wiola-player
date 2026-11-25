/**
 * @file
 * @brief Transactional create, rename, duplicate, and delete playlist commands.
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

#include <wiola/library/repository_transaction.hpp>

#include <functional>
#include <memory>
#include <string>

namespace wiola::app {

struct PlaylistCreation {
    library::PlaylistId id;
    library::CommitResult commit;
};

/// Owner-thread commands. Each return value owns the outcome of that operation.
class SavedPlaylistCommands final {
public:
    using Persist = library::RepositoryTransaction::Persist;

    SavedPlaylistCommands(library::InMemoryRepository& repository, Persist persist);

    [[nodiscard]] PlaylistCreation create(const std::string& name);
    [[nodiscard]] library::CommitResult rename(const library::PlaylistId& id,
        const std::string& name);
    [[nodiscard]] PlaylistCreation duplicate(const library::PlaylistId& id);
    [[nodiscard]] library::CommitResult remove(const library::PlaylistId& id);

private:
    [[nodiscard]] Persist persistence() const;

    library::InMemoryRepository& repository_;
    std::shared_ptr<Persist> persist_;
};

} // namespace wiola::app
