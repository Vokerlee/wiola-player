/**
 * @file
 * @brief Private bounded conversion from FFmpeg frames to interleaved float PCM.
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

#include "conversion_api.hpp"

#include <media/audio_decoder/audio_decoder.hpp>
#include <media/media.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

struct AVFrame;
struct SwrContext;

extern "C" {
#include <libavutil/channel_layout.h>
}

namespace wiola::codec::ffmpeg {

class PcmConverter final {
public:
    explicit PcmConverter(pcm::StreamSpec output, PcmConversionApi api = {}) noexcept;
    ~PcmConverter();

    PcmConverter(const PcmConverter&) = delete;
    PcmConverter& operator=(const PcmConverter&) = delete;
    PcmConverter(PcmConverter&&) = delete;
    PcmConverter& operator=(PcmConverter&&) = delete;

    [[nodiscard]] media::Result<void> submit(const AVFrame& frame);
    [[nodiscard]] media::Result<void> finish();
    [[nodiscard]] media::Result<media::DecodeOutcome> read(std::span<float> output,
        bool source_ended);
    void reset() noexcept;
    void discard_first(pcm::Frames frames) noexcept;
    void truncate(pcm::Frames frames) noexcept;

    [[nodiscard]] bool pending() const noexcept { return offset_ < pending_.size(); }

private:
    [[nodiscard]] media::Result<void> configure(const AVFrame& frame);
    [[nodiscard]] int out_samples(int input_frames) const noexcept;
    [[nodiscard]] int convert(std::uint8_t** output, int output_frames,
        const std::uint8_t* const * input, int input_frames) noexcept;

    pcm::StreamSpec output_;
    PcmConversionApi api_;
    SwrContext* converter_{nullptr};
    AVChannelLayout input_layout_{};
    int input_format_{-1};
    std::vector<float> pending_;
    std::size_t offset_{0};
    bool finished_{false};
};

} // namespace wiola::codec::ffmpeg
