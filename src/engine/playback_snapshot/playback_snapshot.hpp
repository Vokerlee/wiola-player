/**
 * @file
 * @brief Coherent application-facing playback state independent of GUI event types.
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

#include <engine/open_result.hpp>
#include <engine/transport/playback.hpp>
#include <utils/units.hpp>

#include <cstddef>
#include <filesystem>
#include <optional>

namespace wiola::engine {

struct PlaybackSnapshot {
    Playback::State state{Playback::State::idle};
    OpenResult open_result{OpenResult::opened};
    std::filesystem::path track;
    std::optional<std::size_t> queue_position;
    std::size_t structural_revision{0};
    std::size_t metadata_revision{0};
    std::size_t selection_revision{0};
    units::Time time_played{};
    std::optional<units::Time> total_time;
    bool loaded{false};
    bool playing{false};
    bool seekable{false};
};

} // namespace wiola::engine
