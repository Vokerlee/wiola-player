/**
 * @file
 * @brief Per-instance transport scheduling and decode-block configuration.
 * @author Roman Glaz
 * @copyright © 2026, <vokerlee@gmail.com>
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

#include <pcm/stream_spec.hpp>

#include <chrono>

namespace wiola::engine {

/// Owned transport policy. The default is the previous 1024-sample stereo decode block.
struct TransportConfig {
    pcm::Frames decode_chunk{512};
    std::chrono::milliseconds poll_interval{2};

    [[nodiscard]] bool valid() const noexcept
    {
        return decode_chunk > pcm::Frames{} && poll_interval > std::chrono::milliseconds{};
    }
};

} // namespace wiola::engine
