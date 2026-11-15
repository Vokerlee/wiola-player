/**
 * @file
 * @brief Private neutral audio decoder backed by FFmpeg libraries.
 * @author Roman Glaz
 * @copyright © 2026, <vokerlee@gmail.com>
 */

#pragma once

#include "input.hpp"
#include "packet_decoder.hpp"
#include "pcm_converter.hpp"

#include <codec/decode/decoder.hpp>

#include <filesystem>
#include <memory>

namespace wiola::codec::ffmpeg {

class FfmpegDecoder final : public Decoder {
public:
    [[nodiscard]] static media::Result<std::unique_ptr<FfmpegDecoder>>
    open(const std::filesystem::path& path);

private:
    FfmpegDecoder(DecoderStreamInfo info, std::unique_ptr<FfmpegInput> input,
        std::unique_ptr<PacketDecoder> packets) noexcept;

    media::Result<DecodeOutcome> decode(std::span<float> output, pcm::Frames num_frames) override;
    media::Result<SeekOutcome> seek_frame(pcm::Frames frame_index) override;

    std::unique_ptr<FfmpegInput> input_;
    std::unique_ptr<PacketDecoder> packets_;
    PcmConverter converter_;
    bool source_ended_{false};
};

} // namespace wiola::codec::ffmpeg
