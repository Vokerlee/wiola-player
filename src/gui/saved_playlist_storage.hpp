/**
 * @file
 * @brief End-to-end persistent storage for repository-backed saved playlists.
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

#include "library_storage.hpp"

#include <library/in_memory_repository.hpp>

#include <filesystem>

namespace wiola::gui {

class SavedPlaylistStorage final {
public:
    explicit SavedPlaylistStorage(std::filesystem::path file);

    [[nodiscard]] LibraryJsonError save(const library::LibraryRepository& repository);
    [[nodiscard]] LibraryJsonError restore(library::InMemoryRepository& repository) const;

private:
    LibraryStorage storage_;
};

} // namespace wiola::gui
