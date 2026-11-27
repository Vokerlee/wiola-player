/**
 * @file
 * @brief Application-owned playback preferences and one-time restoration.
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

#include <string>
#include <variant>

namespace wiola::gui {

enum StandardTrackColumn {
    cover,
    title,
    artist,
    album,
    length,
    num_columns,
};

struct CustomTagColumn {
    std::string key;
    auto operator<=>(const CustomTagColumn&) const = default;
};

using TrackColumnId = std::variant<StandardTrackColumn, CustomTagColumn>;

struct TrackColumnLayout {
    int width{};
    int order{};
    bool visible{true};
    bool operator==(const TrackColumnLayout&) const = default;
};

} // namespace wiola::gui
