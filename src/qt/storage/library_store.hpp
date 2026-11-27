/**
 * @file
 * @brief Validated library snapshots with atomic persistence and recovery.
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

#include "atomic_file_store.hpp"
#include "library_json.hpp"

#include <wiola/library/in_memory_repository.hpp>

namespace wiola::persistence::qt {

struct LibraryStoreResult {
    LibraryJsonError schema{LibraryJsonError::none};
    FileStoreError io{FileStoreError::none};

    [[nodiscard]] bool succeeded() const noexcept
    {
        return schema == LibraryJsonError::none && io == FileStoreError::none;
    }

    bool operator==(const LibraryStoreResult&) const = default;
};

class LibraryStore final {
public:
    explicit LibraryStore(std::filesystem::path file, AtomicFileStore::OutputFactory output = {});
    [[nodiscard]] LibraryStoreResult load(library::LibrarySnapshot& output) const;
    [[nodiscard]] LibraryStoreResult save(const library::LibrarySnapshot& snapshot);

private:
    AtomicFileStore files_;
};

[[nodiscard]] LibraryStoreResult restore_repository(const LibraryStore& storage,
    library::InMemoryRepository& repository);

} // namespace wiola::persistence::qt
