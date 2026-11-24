/**
 * @file
 * @brief End-to-end identity and transport intent for asynchronous loads.
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

#include <wiola/media/audio_decoder.hpp>
#include <wiola/playback/queue_entry_id.hpp>
#include <wiola/units.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>

namespace wiola::engine {

enum class LoadIntent {
    install,
    play,
};

struct LoadRequest {
    std::uint64_t generation{0};
    QueueEntryId entry;
    std::filesystem::path path;
    LoadIntent intent{LoadIntent::install};
    std::optional<units::Time> position{};

    bool operator==(const LoadRequest&) const = default;
};

struct LoadCompletion {
    LoadRequest request;
    media::DecoderOpenResult opened;
};

} // namespace wiola::engine
