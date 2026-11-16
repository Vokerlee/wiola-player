/**
 * @file
 * @brief Private bounded conversion from FFmpeg frames to interleaved float PCM.
 * @author Roman Glaz
 * @copyright © 2026, <vokerlee@gmail.com>
 */

#pragma once

#include <codec/decode/decoder.hpp>
#include <media/media.hpp>

#include <cstddef>
#include <vector>

struct AVFrame;
struct SwrContext;

extern "C" {
#include <libavutil/channel_layout.h>
}

namespace wiola::codec::ffmpeg {

class PcmConverter final {
public:
    explicit PcmConverter(pcm::StreamSpec output) noexcept;
    ~PcmConverter();

    PcmConverter(const PcmConverter&) = delete;
    PcmConverter& operator=(const PcmConverter&) = delete;
    PcmConverter(PcmConverter&&) = delete;
    PcmConverter& operator=(PcmConverter&&) = delete;

    [[nodiscard]] media::Result<void> submit(const AVFrame& frame);
    [[nodiscard]] media::Result<void> finish();
    [[nodiscard]] media::Result<DecodeOutcome> read(std::span<float> output, bool source_ended);

    [[nodiscard]] bool pending() const noexcept { return offset_ < pending_.size(); }

private:
    [[nodiscard]] media::Result<void> configure(const AVFrame& frame);

    pcm::StreamSpec output_;
    SwrContext* converter_{nullptr};
    AVChannelLayout input_layout_{};
    int input_format_{-1};
    int input_rate_{0};
    std::vector<float> pending_;
    std::size_t offset_{0};
    bool finished_{false};
};

} // namespace wiola::codec::ffmpeg
