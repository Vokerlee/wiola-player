/**
 * @file
 * @brief Frames another thread has already decoded.
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

#include <audio/dsp/source.hpp>
#include <pcm/pcm_queue.hpp>
#include <wiola/media/stream_spec.hpp>

#include <cstddef>
#include <span>

namespace wiola::audio {

/// What a PCM queue holds. Short whenever whoever fills it has not kept up.
class BufferSource final : public Source {
public:
    BufferSource(pcm::StreamSpec spec, pcm::PcmQueue::Consumer buffer) noexcept;

    std::size_t render(std::span<float> interleaved) noexcept override;

    [[nodiscard]] pcm::StreamSpec spec() const noexcept override;

private:
    pcm::StreamSpec spec_;
    pcm::PcmQueue::Consumer buffer_;
};

} // namespace wiola::audio
