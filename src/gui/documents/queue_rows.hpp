/**
 * @file
 * @brief Explicit playback-queue conversion at the Now Playing presentation boundary.
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
#include <engine/queue/track.hpp>
#include <gui/tracks/track_row.hpp>
#include <span>

namespace wiola::gui {

[[nodiscard]] TrackRow queue_row(const engine::Track& track);
[[nodiscard]] std::vector<TrackRow> queue_rows(std::span<const engine::Track> tracks);
[[nodiscard]] std::vector<engine::QueueEntryId> queue_entry_ids(std::span<const TrackRowId> rows);
[[nodiscard]] std::optional<engine::QueueEntryId>
queue_entry_id(const std::optional<TrackRowId>& row);

} // namespace wiola::gui
