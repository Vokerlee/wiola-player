/**
 * @file
 * @brief User-facing names for saved playlists and legacy defaults.
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

#include <QString>
#include <wiola/library/records.hpp>

namespace wiola::gui {

[[nodiscard]] inline QString playlist_name(const library::PlaylistRecord& playlist)
{
    if (playlist.id.value == "playlist-migrated-queue" && playlist.name == "Migrated queue")
        return "My playlist";
    return QString::fromStdString(playlist.name);
}

} // namespace wiola::gui
