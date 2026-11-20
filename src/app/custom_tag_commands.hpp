/**
 * @file
 * @brief Transactional custom tag add, edit, and remove commands.
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

#include <functional>

namespace wiola::app {

enum class CustomTagError {
    none,
    missing_media,
    missing_tag,
    invalid_value,
    duplicate_value,
    limit_exceeded,
    persistence_failed,
};

struct CustomTagResult {
    CustomTagError error{CustomTagError::none};
    library::TagId tag;
    bool changed{false};

    [[nodiscard]] bool succeeded() const noexcept { return error == CustomTagError::none; }
};

class CustomTagCommands final {
public:
    using Persist = std::function<bool(const library::LibraryRepository&)>;

    CustomTagCommands(library::InMemoryRepository& repository, Persist persist);

    [[nodiscard]] CustomTagResult add(const library::MediaId& media, std::string key,
        std::string value);
    [[nodiscard]] CustomTagResult edit(const library::MediaId& media, const library::TagId& tag,
        std::string key, std::string value);
    [[nodiscard]] CustomTagResult remove(const library::MediaId& media, const library::TagId& tag);

private:
    struct Snapshot {
        std::vector<library::TrackRecord> tracks;
        std::vector<library::PlaylistRecord> playlists;
        std::vector<library::WorkspaceRecord> workspaces;
    };

    [[nodiscard]] Snapshot snapshot() const;
    [[nodiscard]] CustomTagResult commit(const Snapshot& previous, const library::TagId& tag);

    library::InMemoryRepository& repository_;
    Persist persist_;
};

} // namespace wiola::app
