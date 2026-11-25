/**
 * @file
 * @brief Bounded UTF-8 M3U8 import and export for saved playlists.
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

#include <library/in_memory_repository.hpp>

#include <QByteArray>

#include <filesystem>

namespace wiola::app {

enum class M3uError {
    none,
    missing_playlist,
    too_large,
    malformed,
    limit_exceeded,
    invalid_record,
};

class M3uPlaylist final {
public:
    [[nodiscard]] static M3uError export_playlist(const library::LibraryRepository& repository,
        const library::PlaylistId& id, const std::filesystem::path& directory, QByteArray& output);

    [[nodiscard]] static M3uError import_playlist(library::InMemoryRepository& repository,
        const std::filesystem::path& directory, const std::string& name, QByteArrayView input,
        library::PlaylistId& output);
};

} // namespace wiola::app
