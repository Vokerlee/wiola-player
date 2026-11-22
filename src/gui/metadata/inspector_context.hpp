/**
 * @file
 * @brief Owned inspector selection snapshots and explicit playing fallback.
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
#include <gui/tracks/track_row.hpp>

namespace wiola::app {

class LibrarySession;

}

namespace wiola::gui {

struct InspectorContext {
    enum class Origin {
        none,
        selection,
        playing,
    };
    Origin origin{Origin::none};
    std::vector<TrackRow> tracks;
    // Small, owned selection snapshots. No repository pointers survive presentation.
    std::vector<library::TrackRecord> editable;
};

/// Playing fallback is opt-in; an explicit document selection always takes precedence.
[[nodiscard]] InspectorContext inspector_context(const app::LibrarySession& library,
    std::vector<SelectedTrack> selection, std::optional<TrackRow> playing = {});

} // namespace wiola::gui
