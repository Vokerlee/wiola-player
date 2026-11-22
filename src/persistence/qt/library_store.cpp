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

#include "library_store.hpp"
#include <utility>

namespace wiola::persistence::qt {
namespace {

struct Candidate {
    library::LibrarySnapshot snapshot;
    LibraryStoreResult result;
    bool exists{false};
};

Candidate decode(const FileReadResult& file)
{
    Candidate result;
    result.exists = file.exists;
    result.result.io = file.error;
    if (file.exists && file.error == FileStoreError::none)
        result.result.schema = read_library_json(file.bytes, result.snapshot);
    return result;
}

} // namespace

LibraryStore::LibraryStore(std::filesystem::path file, AtomicFileStore::OutputFactory output)
    : files_{std::move(file), max_library_document_bytes, std::move(output)}
{
}

LibraryStoreResult LibraryStore::load(library::LibrarySnapshot& output) const
{
    auto primary = decode(files_.read(FileSlot::primary));
    if (primary.exists && primary.result.succeeded()) {
        output = std::move(primary.snapshot);
        return {};
    }
    if (primary.result.schema == LibraryJsonError::unsupported_version)
        return primary.result;
    auto backup = decode(files_.read(FileSlot::backup));
    if (backup.result.schema == LibraryJsonError::unsupported_version)
        return backup.result;
    if (backup.exists && backup.result.succeeded()) {
        output = std::move(backup.snapshot);
        return {};
    }
    if (primary.exists || !primary.result.succeeded())
        return primary.result;
    if (backup.exists || !backup.result.succeeded())
        return backup.result;
    output = {};
    return {};
}

LibraryStoreResult LibraryStore::save(const library::LibrarySnapshot& snapshot)
{
    QByteArray json;
    const auto serialized = write_library_json(snapshot, json);
    if (serialized != LibraryJsonError::none)
        return {.schema = serialized};
    const auto primary = files_.read(FileSlot::primary);
    const auto current = decode(primary);
    if (current.result.io != FileStoreError::none ||
        current.result.schema == LibraryJsonError::unsupported_version)
        return current.result;
    const auto backup = decode(files_.read(FileSlot::backup));
    if (backup.result.io != FileStoreError::none ||
        backup.result.schema == LibraryJsonError::unsupported_version)
        return backup.result;
    if (current.exists && current.result.succeeded()) {
        const auto error = files_.write(FileSlot::backup, primary.bytes);
        if (error != FileStoreError::none)
            return {.io = error};
    }
    return {.io = files_.write(FileSlot::primary, json)};
}

LibraryStoreResult restore_repository(const LibraryStore& storage,
    library::InMemoryRepository& repository)
{
    library::LibrarySnapshot snapshot;
    const auto result = storage.load(snapshot);
    if (!result.succeeded())
        return result;
    return repository.replace_all(snapshot.tracks, snapshot.playlists, snapshot.workspaces)
        ? LibraryStoreResult{}
        : LibraryStoreResult{.schema = LibraryJsonError::invalid_record};
}

} // namespace wiola::persistence::qt
