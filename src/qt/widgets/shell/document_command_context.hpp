/**
 * @file
 * @brief A length of time, written the way a listener reads it.
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
#include <optional>
#include <vector>
#include <wiola/presentation/documents/document_id.hpp>
#include <wiola/presentation/tracks/track_commands.hpp>
#include <wiola/presentation/tracks/track_identity.hpp>

namespace wiola::gui {

/// Owned user intent, stable across focus changes and modal destination prompts.
struct DocumentCommandContext {
    DocumentId document;
    TrackCapabilities capabilities;
    std::vector<SelectedTrack> selection;
    std::vector<std::filesystem::path> displayed_paths;
    std::optional<library::PlaylistId> active_playlist;
    std::optional<library::PlaylistId> target_playlist;
};

} // namespace wiola::gui
