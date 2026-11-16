/**
 * @file
 * @brief Private neutral audio decoder backed by FFmpeg libraries.
 * @author Roman Glaz
 * @copyright © 2026, <vokerlee@gmail.com>
 */

#pragma once

#include <codec/decode/decoder.hpp>
#include <codec/decode/format.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>

struct AVFrame;

namespace wiola::codec::ffmpeg {

class FfmpegInput;
class PacketDecoder;
class PcmConverter;

[[nodiscard]] Backend backend();

class FfmpegDecoder final : public Decoder {
public:
    [[nodiscard]] static media::Result<std::unique_ptr<FfmpegDecoder>>
    open(const std::filesystem::path& path);
    ~FfmpegDecoder() override;

private:
    FfmpegDecoder(std::filesystem::path path, std::size_t stream_index, DecoderStreamInfo info,
        std::unique_ptr<FfmpegInput> input, std::unique_ptr<PacketDecoder> packets) noexcept;

    media::Result<DecodeOutcome> decode(std::span<float> output, pcm::Frames num_frames) override;
    media::Result<SeekOutcome> seek_frame(pcm::Frames frame_index) override;
    [[nodiscard]] media::Result<pcm::Frames> stage_seek(pcm::Frames target);
    [[nodiscard]] media::Result<bool> trim_frame(const AVFrame& frame,
        std::optional<pcm::Frames> preroll = {}, std::int64_t timestamp_offset = 0,
        std::optional<std::int64_t> sequential_start = {});

    std::filesystem::path path_;
    std::size_t stream_index_;
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
