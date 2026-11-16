/**
 * @file
 * @brief Private bounded FFmpeg packet submission, frame reception, and draining.
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

#include "input.hpp"

#include <media/media.hpp>

#include <cstddef>
#include <memory>

struct AVCodecContext;
struct AVFrame;
struct AVPacket;

namespace wiola::codec::ffmpeg {

struct FfmpegFrameEvent {
    const AVFrame* frame{nullptr};
    bool end_of_stream{false};
};

class PacketDecoder final {
public:
    [[nodiscard]] static media::Result<std::unique_ptr<PacketDecoder>> open(FfmpegInput& input,
        const FfmpegStreamFacts& facts);
    ~PacketDecoder();

    PacketDecoder(const PacketDecoder&) = delete;
    PacketDecoder& operator=(const PacketDecoder&) = delete;
    PacketDecoder(PacketDecoder&&) = delete;
    PacketDecoder& operator=(PacketDecoder&&) = delete;

    [[nodiscard]] media::Result<FfmpegFrameEvent> next_frame();
    void reset_after_seek() noexcept;

    [[nodiscard]] AVCodecContext* codec() const noexcept { return codec_; }

private:
    PacketDecoder(FfmpegInput& input, std::size_t stream_index) noexcept;
    [[nodiscard]] media::Result<void> initialize();
    [[nodiscard]] media::Result<FfmpegFrameEvent> error(media::Error error);

    FfmpegInput& input_;
    std::size_t stream_index_;
    AVCodecContext* codec_{nullptr};
    AVPacket* packet_{nullptr};
    AVFrame* frame_{nullptr};
    bool packet_pending_{false};
    bool demux_ended_{false};
    bool drain_sent_{false};
    bool ended_{false};
};

} // namespace wiola::codec::ffmpeg
