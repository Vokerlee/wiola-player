/**
 * @file
 * @brief Private neutral audio decoder backed by FFmpeg libraries.
 * @author Roman Glaz
 * @copyright © 2026, <vokerlee@gmail.com>
 */

#include "decoder.hpp"

#include "input.hpp"
#include "packet_decoder.hpp"
#include "pcm_converter.hpp"

#include <media/local_file_stream.hpp>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/mathematics.h>
}

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>

namespace wiola::codec::ffmpeg {

namespace {

media::Error seek_error(const char* detail)
{
    return media::Error{.category = media::ErrorCategory::seek_failed, .detail = detail};
}

std::optional<std::int64_t> frame_position(const AVFrame& frame, const AVFormatContext& input,
    std::size_t stream_index, int sample_rate)
{
    if (frame.best_effort_timestamp == AV_NOPTS_VALUE)
        return std::nullopt;
    const AVStream* stream{input.streams[stream_index]};
    const AVRational frame_rate{1, sample_rate};
    std::int64_t position{av_rescale_q(frame.best_effort_timestamp, stream->time_base, frame_rate)};
    const std::int64_t encoded_origin{
        stream->start_time != AV_NOPTS_VALUE ? stream->start_time : input.start_time};
    if (encoded_origin != AV_NOPTS_VALUE) {
        const AVRational origin_base{
            input.start_time == encoded_origin && stream->start_time == AV_NOPTS_VALUE
                ? AV_TIME_BASE_Q
                : stream->time_base};
        position -= av_rescale_q(encoded_origin, origin_base, frame_rate);
    }
    return position;
}

} // namespace

Backend backend()
{
    static const std::array markers{
        Marker{0, "OggS"}
    };
    static constexpr std::array<std::string_view, 3> extensions{".ogg", ".oga", ".opus"};
    static const std::array formats{
        Format{"Ogg Vorbis/Opus", markers, extensions, {}}
    };
    return Backend{"FFmpeg", formats, [](const std::filesystem::path& path) -> Opened {
                       auto decoder{FfmpegDecoder::open(path)};
                       if (!decoder)
                           return media::unexpected{decoder.error()};
                       return std::unique_ptr<Decoder>{std::move(*decoder)};
                   }};
}

FfmpegDecoder::FfmpegDecoder(std::filesystem::path path, std::size_t stream_index,
    DecoderStreamInfo info, std::unique_ptr<FfmpegInput> input,
    std::unique_ptr<PacketDecoder> packets) noexcept
    : Decoder{info}
    , path_{std::move(path)}
    , stream_index_{stream_index}
    , input_{std::move(input)}
    , packets_{std::move(packets)}
    , converter_{std::make_unique<PcmConverter>(info.spec)}
{
}

FfmpegDecoder::~FfmpegDecoder() = default;

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
        new FfmpegDecoder{path, facts->stream_index, facts->output, std::move(*input),
                          std::move(*packets)}
    };
}

media::Result<bool> FfmpegDecoder::trim_frame(const AVFrame& frame,
    std::optional<pcm::Frames> preroll, std::int64_t timestamp_offset)
{
    const pcm::Frames reference{preroll.value_or(position_frames())};
    auto start{preroll ? frame_position(frame, *input_->format(), stream_index_,
                             static_cast<int>(spec().sample_rate.hz()))
                       : std::nullopt};
    if (start)
        *start -= timestamp_offset;

    if (start) {
        const std::int64_t end{*start + frame.nb_samples};
        if (end <= static_cast<std::int64_t>(reference.count()))
            return false;
    }

    auto submitted{converter_->submit(frame)};
    if (!submitted)
        return media::unexpected{submitted.error()};

    if (start && *start < static_cast<std::int64_t>(reference.count())) {
        const auto audible_start{static_cast<std::uint64_t>(std::max<std::int64_t>(*start, 0))};
        const auto discard{static_cast<std::uint64_t>(reference.count()) - audible_start};
        if (discard <= std::numeric_limits<std::size_t>::max())
            converter_->discard_first(pcm::Frames{static_cast<std::size_t>(discard)});
    }

    if (length_accuracy() == LengthAccuracy::exact) {
        const pcm::Frames length{num_frames()};
        const pcm::Frames remaining{reference < length ? length - reference : pcm::Frames{}};
        converter_->truncate(remaining);
    }
    return true;
}

media::Result<DecodeOutcome> FfmpegDecoder::decode(std::span<float> output, pcm::Frames num_frames)
{
    const std::span<float> requested{output.first(spec().samples_per(num_frames))};
    for (int work{0}; work < 64; ++work) {
        if (!converter_->pending() && !source_ended_) {
            auto event{packets_->next_frame()};
            if (!event)
                return media::unexpected{event.error()};
            if (event->end_of_stream) {
                source_ended_ = true;
            } else {
                auto trimmed{trim_frame(*event->frame)};
                if (!trimmed)
                    return media::unexpected{trimmed.error()};
                if (!*trimmed)
                    continue;
            }
        }

        if (source_ended_ && !converter_->pending()) {
            auto finished{converter_->finish()};
            if (!finished)
                return media::unexpected{finished.error()};
        }

        auto converted{converter_->read(requested, source_ended_)};
        if (!converted || converted->frames != pcm::Frames{} || converted->end_of_stream)
            return converted;
    }

    return media::unexpected{
        media::Error{.category = media::ErrorCategory::limit_exceeded,
                     .detail = "FFmpeg conversion exceeded its progress budget"}
    };
}

media::Result<pcm::Frames> FfmpegDecoder::stage_seek(pcm::Frames target)
{
    std::int64_t timestamp_offset{0};
    auto first{packets_->next_frame()};
    if (!first)
        return media::unexpected{first.error()};
    if (first->end_of_stream)
        return media::unexpected{seek_error("seek target is beyond actual audio")};
    const auto initial_position{frame_position(*first->frame, *input_->format(), stream_index_,
        static_cast<int>(spec().sample_rate.hz()))};
    if (initial_position && *initial_position > 0)
        timestamp_offset = *initial_position;

    AVStream* stream{input_->format()->streams[stream_index_]};
    const AVRational output_rate{1, static_cast<int>(spec().sample_rate.hz())};
    if (target.count() > static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max()) ||
        timestamp_offset >
            std::numeric_limits<std::int64_t>::max() - static_cast<std::int64_t>(target.count()))
        return media::unexpected{seek_error("seek frame exceeds FFmpeg timestamps")};
    const std::int64_t seek_sample{static_cast<std::int64_t>(target.count()) + timestamp_offset};
    std::int64_t timestamp{av_rescale_q(seek_sample, output_rate, stream->time_base)};
    const std::int64_t origin{stream->start_time != AV_NOPTS_VALUE ? stream->start_time : 0};
    if ((origin > 0 && timestamp > std::numeric_limits<std::int64_t>::max() - origin) ||
        (origin < 0 && timestamp < std::numeric_limits<std::int64_t>::min() - origin))
        return media::unexpected{seek_error("seek timestamp overflow")};
    timestamp += origin;
    if (av_seek_frame(input_->format(), static_cast<int>(stream_index_), timestamp,
            AVSEEK_FLAG_BACKWARD) < 0)
        return media::unexpected{seek_error("FFmpeg demuxer rejected the seek")};

    packets_->reset_after_seek();
    converter_->reset();
    source_ended_ = false;

    if (length_accuracy() == LengthAccuracy::exact && target == num_frames())
        return target;

    for (int work{0}; work < 65536; ++work) {
        auto event{packets_->next_frame()};
        if (!event)
            return media::unexpected{event.error()};
        if (event->end_of_stream)
            return media::unexpected{seek_error("seek target is beyond actual audio")};
        if (event->frame->best_effort_timestamp == AV_NOPTS_VALUE && target != pcm::Frames{})
            return media::unexpected{seek_error("stream lacks sample-accurate seek timestamps")};
        auto trimmed{trim_frame(*event->frame, target, timestamp_offset)};
        if (!trimmed)
            return media::unexpected{trimmed.error()};
        if (*trimmed && converter_->pending())
            return target;
    }
    return media::unexpected{
        media::Error{.category = media::ErrorCategory::limit_exceeded,
                     .detail = "seek preroll exceeded its budget"}
    };
}

media::Result<SeekOutcome> FfmpegDecoder::seek_frame(pcm::Frames frame_index)
{
    auto candidate{FfmpegDecoder::open(path_)};
    if (!candidate)
        return media::unexpected{candidate.error()};
    auto staged{(*candidate)->stage_seek(frame_index)};
    if (!staged)
        return media::unexpected{staged.error()};

    input_.swap((*candidate)->input_);
    packets_.swap((*candidate)->packets_);
    converter_.swap((*candidate)->converter_);
    source_ended_ = false;
    return SeekOutcome{.target = frame_index, .achieved = *staged, .exact = true};
}

} // namespace wiola::codec::ffmpeg
