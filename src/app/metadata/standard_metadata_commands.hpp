/**
 * @file
 * @brief Transactional application-level standard metadata overrides.
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

#include <library/metadata_patch.hpp>

#include <library/in_memory_repository.hpp>

#include <functional>

namespace wiola::app {

enum class StandardMetadataError {
    none,
    missing_media,
    empty_edit,
    invalid_edit,
    persistence_failed,
};

struct StandardMetadataResult {
    StandardMetadataError error{StandardMetadataError::none};
    bool changed{false};

    [[nodiscard]] bool succeeded() const noexcept { return error == StandardMetadataError::none; }
};

class StandardMetadataCommands final {
public:
    using Persist = std::function<bool(const library::LibraryRepository&)>;

    StandardMetadataCommands(library::InMemoryRepository& repository, Persist persist);

    [[nodiscard]] StandardMetadataResult apply(const library::MediaId& media,
        const library::StandardMetadataPatch& edit);

private:
    library::InMemoryRepository& repository_;
    Persist persist_;
};

} // namespace wiola::app
