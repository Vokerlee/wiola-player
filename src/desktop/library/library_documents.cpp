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

#include "library_documents.hpp"
#include <QFileInfo>
#include <persistence/qt/atomic_file_store.hpp>
#include <qt/paths.hpp>

namespace wiola::app {

LibraryDocuments::LibraryDocuments(library::InMemoryRepository& repository,
    library::RepositoryTransaction::Persist persist)
    : repository_{repository}
    , persist_{std::move(persist)}
{
}

WorkspaceCreation LibraryDocuments::open_folder(const std::filesystem::path& root)
{
    if (root.empty())
        return {LibraryDocumentError::invalid, {}};
    library::RepositoryTransaction transaction{repository_, persist_};
    const auto id = repository_.create_workspace(root);
    if (!id.valid())
        return {LibraryDocumentError::invalid, {}};
    if (!transaction.commit())
        return {LibraryDocumentError::write_failed, {}};
    return {LibraryDocumentError::none, id};
}

LibraryDocumentError LibraryDocuments::set_recursive(const library::WorkspaceId& id, bool recursive)
{
    const auto* current = repository_.workspace(id);
    if (!current)
        return LibraryDocumentError::missing;
    if (current->recursive == recursive)
        return LibraryDocumentError::none;
    auto next = *current;
    next.recursive = recursive;
    library::RepositoryTransaction transaction{repository_, persist_};
    if (!repository_.put_workspace(std::move(next)))
        return LibraryDocumentError::invalid;
    return transaction.commit() ? LibraryDocumentError::none : LibraryDocumentError::write_failed;
}

PlaylistImport LibraryDocuments::import_m3u(const std::filesystem::path& path)
{
    persistence::qt::AtomicFileStore store{path, 16 * 1024 * 1024};
    const auto input = store.read(persistence::qt::FileSlot::primary);
    if (input.error != persistence::qt::FileStoreError::none || !input.exists)
        return {LibraryDocumentError::read_failed, {}};
    const QFileInfo info{platform::qt::QtPathAdapter::to_qt(path)};
    library::RepositoryTransaction transaction{repository_, persist_};
    library::PlaylistId id;
    if (M3uPlaylist::import_playlist(repository_, path.parent_path(),
            info.completeBaseName().toStdString(), input.bytes, id) != M3uError::none)
        return {LibraryDocumentError::invalid, {}};
    if (!transaction.commit())
        return {LibraryDocumentError::write_failed, {}};
    return {LibraryDocumentError::none, id};
}

LibraryDocumentError LibraryDocuments::export_m3u(const library::PlaylistId& id,
    const std::filesystem::path& path) const
{
    QByteArray bytes;
    if (M3uPlaylist::export_playlist(repository_, id, path.parent_path(), bytes) != M3uError::none)
        return LibraryDocumentError::invalid;
    persistence::qt::AtomicFileStore store{path, 16 * 1024 * 1024};
    return store.write(persistence::qt::FileSlot::primary, bytes) ==
            persistence::qt::FileStoreError::none
        ? LibraryDocumentError::none
        : LibraryDocumentError::write_failed;
}

} // namespace wiola::app
