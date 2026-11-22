/**
 * @file
 * @brief Typed track sort fields, directions, and values.
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

#include "track_row.hpp"
#include <library/repository.hpp>
#include <utils/units.hpp>

#include <string>
#include <variant>

namespace wiola::gui {

enum class TrackSortField {
    title,
    artist,
    album,
    duration,
    path,
    custom_tag,
};

enum class TrackSortDirection {
    ascending,
    descending,
};

struct TrackSortKey {
    TrackSortField field{TrackSortField::title};
    TrackSortDirection direction{TrackSortDirection::ascending};
    std::string tag_key;

    [[nodiscard]] static TrackSortKey for_tag(std::string key,
        TrackSortDirection direction = TrackSortDirection::ascending);

    [[nodiscard]] bool valid() const;

    bool operator==(const TrackSortKey&) const = default;
};

using TrackSortValue = std::variant<std::monostate, std::string, units::Time>;

[[nodiscard]] TrackSortValue track_sort_value(const TrackSortKey& key, const TrackRow& track,
    const library::LibraryRepository* repository);

} // namespace wiola::gui
