/**
 * @file
 * @brief Private neutral audio decoder backed by FFmpeg libraries.
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
#include "packet_decoder.hpp"

#include <codec/decode/decoder.hpp>
#include <codec/decode/format.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>

struct AVFrame;

namespace wiola::codec::ffmpeg {

class FfmpegInput;
class PcmConverter;

using DemuxSeek = int (*)(AVFormatContext*, int, std::int64_t, int);

struct FfmpegDecoderOptions {
    PacketDecoderOptions packets;
    PcmConversionApi conversion;
    StreamInfoReader inspect{nullptr};
    DemuxSeek seek{nullptr};
    int decode_work_budget{64};
    int seek_work_budget{65536};
};

[[nodiscard]] Backend backend();

class FfmpegDecoder final : public Decoder {
public:
    [[nodiscard]] static media::Result<std::unique_ptr<FfmpegDecoder>>
    open(const std::filesystem::path& path, FfmpegDecoderOptions options = {});
    ~FfmpegDecoder() override;

private:
    FfmpegDecoder(std::filesystem::path path, std::size_t stream_index, DecoderStreamInfo info,
        std::unique_ptr<FfmpegInput> input, std::unique_ptr<PacketDecoder> packets,
        FfmpegDecoderOptions options) noexcept;

    media::Result<DecodeOutcome> decode(std::span<float> output, pcm::Frames num_frames) override;
    media::Result<SeekOutcome> seek_frame(pcm::Frames frame_index) override;
    [[nodiscard]] media::Result<pcm::Frames> stage_seek(pcm::Frames target);
    [[nodiscard]] media::Result<bool> trim_frame(const AVFrame& frame,
        std::optional<pcm::Frames> preroll = {}, std::int64_t timestamp_offset = 0,
        std::optional<std::int64_t> sequential_start = {});
    [[nodiscard]] int demux_seek(std::int64_t timestamp) noexcept;

    std::filesystem::path path_;
    std::size_t stream_index_;
    FfmpegDecoderOptions options_;
    std::unique_ptr<FfmpegInput> input_;
    std::unique_ptr<PacketDecoder> packets_;
    std::unique_ptr<PcmConverter> converter_;
    std::optional<std::int64_t> audible_timestamp_origin_;
    std::optional<std::int64_t> previous_relative_position_;
    pcm::Frames link_base_{};
    int fresh_frame_samples_{0};
    bool source_ended_{false};
};

} // namespace wiola::codec::ffmpeg
