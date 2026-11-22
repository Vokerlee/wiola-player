/**
 * @file
 * @brief One normalized search, filter, and sort state for each track browser.
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
#include "track_sort.hpp"
#include <QHeaderView>
#include <array>
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

struct TrackColumnDescriptor {
    TrackColumnId id;
    std::string label;
    int width;
    QHeaderView::ResizeMode resize;
    std::optional<TrackSortKey> sort;
    int role{Qt::DisplayRole};
};

[[nodiscard]] const std::array<TrackColumnDescriptor, num_columns>& standard_track_columns();
[[nodiscard]] TrackColumnDescriptor tag_track_column(std::string label);

struct TrackColumnLayout {
    int width{};
    int order{};
    bool visible{true};
};

} // namespace wiola::gui
