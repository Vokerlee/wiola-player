/**
 * @file
 * @brief Distinct occurrence identities shared by document selections and track rows.
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
#include <engine/queue/queue_entry_id.hpp>
#include <filesystem>
#include <library/identity.hpp>
#include <variant>

namespace wiola::gui {

struct WorkspaceTrackId {
    library::WorkspaceId workspace;
    library::MediaId media;
    auto operator<=>(const WorkspaceTrackId&) const = default;
};

using TrackRowId = std::variant<engine::QueueEntryId, library::PlaylistEntryId, WorkspaceTrackId>;

struct SelectedTrack {
    library::MediaId media;
    TrackRowId occurrence;
    std::filesystem::path location;
    bool operator==(const SelectedTrack&) const = default;
};

} // namespace wiola::gui
