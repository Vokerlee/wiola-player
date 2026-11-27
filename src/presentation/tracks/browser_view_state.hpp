/**
 * @file
 * @brief Portable browser columns, query and occurrence selection state.
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

#include "track_column_identity.hpp"
#include "track_identity.hpp"
#include "track_view_state.hpp"

#include <map>
#include <optional>
#include <vector>

namespace wiola::gui {

struct BrowserViewState {
    TrackViewState view;
    std::map<TrackColumnId, TrackColumnLayout> columns;
    std::vector<TrackRowId> selected;
    std::optional<TrackRowId> current, anchor;
    int anchor_offset{};
    bool operator==(const BrowserViewState&) const = default;
};

} // namespace wiola::gui
