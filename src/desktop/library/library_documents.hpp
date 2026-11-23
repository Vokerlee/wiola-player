/**
 * @file
 * @brief A length of time, written the way a listener reads it.
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
#include <desktop/library/m3u_playlist.hpp>
#include <library/repository_transaction.hpp>

namespace wiola::app {

enum class LibraryDocumentError {
    none,
    missing,
    invalid,
    read_failed,
    write_failed,
};

struct WorkspaceCreation {
    LibraryDocumentError error;
    library::WorkspaceId id;
};

struct PlaylistImport {
    LibraryDocumentError error;
    library::PlaylistId id;
};

/// Transactional document use cases. Views never construct repository transactions.
class LibraryDocuments final {
public:
    LibraryDocuments(library::InMemoryRepository& repository,
        library::RepositoryTransaction::Persist persist);
    [[nodiscard]] WorkspaceCreation open_folder(const std::filesystem::path& root);
    [[nodiscard]] LibraryDocumentError set_recursive(const library::WorkspaceId& id,
        bool recursive);
    [[nodiscard]] PlaylistImport import_m3u(const std::filesystem::path& path);
    [[nodiscard]] LibraryDocumentError export_m3u(const library::PlaylistId& id,
        const std::filesystem::path& path) const;

private:
    library::InMemoryRepository& repository_;
    library::RepositoryTransaction::Persist persist_;
};

} // namespace wiola::app
