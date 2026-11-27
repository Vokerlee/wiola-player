/**
 * @file
 * @brief Typed queue snapshots and the storage port used by restoration.
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

#include <wiola/playback/repeat_mode.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace wiola::app {

struct QueueSnapshot {
    std::vector<std::filesystem::path> tracks;
    std::optional<std::filesystem::path> selected;
    engine::RepeatMode repeat{engine::RepeatMode::none};
    bool shuffled{false};
    std::vector<std::uint64_t> occurrence_ids;
    std::optional<std::uint64_t> selected_occurrence;
    bool has_occurrence_ids{false};
};

class QueueSettingsStore {
public:
    virtual ~QueueSettingsStore() = default;
    /// Decoders reject invalid occurrence tokens as an empty list while preserving
    /// has_occurrence_ids, so restoration cannot fall back to ambiguous legacy paths.
    [[nodiscard]] virtual QueueSnapshot read_queue() const = 0;
    virtual void write_queue(const QueueSnapshot& snapshot) = 0;
};

} // namespace wiola::app
