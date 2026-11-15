/**
 * @file
 * @brief Private neutral audio decoder backed by FFmpeg libraries.
 * @author Roman Glaz
 * @copyright © 2026, <vokerlee@gmail.com>
 */

#include "decoder.hpp"

#include <media/local_file_stream.hpp>

namespace wiola::codec::ffmpeg {

FfmpegDecoder::FfmpegDecoder(DecoderStreamInfo info, std::unique_ptr<FfmpegInput> input,
    std::unique_ptr<PacketDecoder> packets) noexcept
    : Decoder{info}
    , input_{std::move(input)}
    , packets_{std::move(packets)}
    , converter_{info.spec}
{
}

media::Result<std::unique_ptr<FfmpegDecoder>> FfmpegDecoder::open(const std::filesystem::path& path)
{
    auto stream{media::LocalFileStream::open(path)};
    if (!stream)
        return media::unexpected{stream.error()};
    auto input{FfmpegInput::open(std::move(*stream))};
    if (!input)
        return media::unexpected{input.error()};
    auto facts{(*input)->inspect()};
    if (!facts)
        return media::unexpected{facts.error()};
    auto packets{PacketDecoder::open(**input, *facts)};
    if (!packets)
        return media::unexpected{packets.error()};

    return std::unique_ptr<FfmpegDecoder>{
        new FfmpegDecoder{facts->output, std::move(*input), std::move(*packets)}
    };
}

media::Result<DecodeOutcome> FfmpegDecoder::decode(std::span<float> output, pcm::Frames num_frames)
{
    const std::span<float> requested{output.first(spec().samples_per(num_frames))};
    for (int work{0}; work < 64; ++work) {
        if (!converter_.pending() && !source_ended_) {
            auto event{packets_->next_frame()};
            if (!event)
                return media::unexpected{event.error()};
            if (event->end_of_stream)
                source_ended_ = true;
            else {
                auto submitted{converter_.submit(*event->frame)};
                if (!submitted)
                    return media::unexpected{submitted.error()};
            }
        }

        if (source_ended_ && !converter_.pending()) {
            auto finished{converter_.finish()};
            if (!finished)
                return media::unexpected{finished.error()};
        }

        auto converted{converter_.read(requested, source_ended_)};
        if (!converted || converted->frames != pcm::Frames{} || converted->end_of_stream)
            return converted;
    }

    return media::unexpected{
        media::Error{.category = media::ErrorCategory::limit_exceeded,
                     .detail = "FFmpeg conversion exceeded its progress budget"}
    };
}

media::Result<SeekOutcome> FfmpegDecoder::seek_frame(pcm::Frames)
{
    return media::unexpected{
        media::Error{.category = media::ErrorCategory::seek_failed,
                     .detail = "FFmpeg seek is unavailable"}
    };
}

} // namespace wiola::codec::ffmpeg
