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
    static const std::array ogg_markers{
        Marker{0, "OggS"}
    };
    static const std::array mp4_markers{
        Marker{4, "ftyp"}
    };
    static const std::array aiff_markers{
        Marker{0, "FORM"},
        Marker{8, "AIFF"}
    };
    static const std::array aifc_markers{
        Marker{0, "FORM"},
        Marker{8, "AIFC"}
    };
    static const std::array au_markers{
        Marker{0, ".snd"}
    };
    static const std::array caf_markers{
        Marker{0, "caff"}
    };
    static const std::array w64_markers{
        Marker{0, "riff"}
    };
    static const std::array rf64_markers{
        Marker{0, "RF64"},
        Marker{8, "WAVE"}
    };
    static const std::array wavpack_markers{
        Marker{0, "wvpk"}
    };
    static const std::array ape_markers{
        Marker{0, "MAC "}
    };
    static const std::array tta_markers{
        Marker{0, "TTA1"}
    };
    static const std::array asf_markers{
        Marker{0, "\x30\x26\xb2\x75"}
    };
    static const std::array ebml_markers{
        Marker{0, "\x1a\x45\xdf\xa3"}
    };
    static const std::array wav_markers{
        Marker{0, "RIFF"},
        Marker{8, "WAVE"}
    };
    static const std::array flac_markers{
        Marker{0, "fLaC"}
    };
    static constexpr std::array<std::string_view, 3> ogg_extensions{".ogg", ".oga", ".opus"};
    static constexpr std::array<std::string_view, 2> mp4_extensions{".m4a", ".mp4"};
    static constexpr std::array<std::string_view, 1> adts_extensions{".aac"};
    static constexpr std::array<std::string_view, 2> aiff_extensions{".aif", ".aiff"};
    static constexpr std::array<std::string_view, 1> aifc_extensions{".aifc"};
    static constexpr std::array<std::string_view, 1> au_extensions{".au"};
    static constexpr std::array<std::string_view, 1> caf_extensions{".caf"};
    static constexpr std::array<std::string_view, 1> w64_extensions{".w64"};
    static constexpr std::array<std::string_view, 1> rf64_extensions{".rf64"};
    static constexpr std::array<std::string_view, 1> wavpack_extensions{".wv"};
    static constexpr std::array<std::string_view, 1> ape_extensions{".ape"};
    static constexpr std::array<std::string_view, 1> tta_extensions{".tta"};
    static constexpr std::array<std::string_view, 1> asf_extensions{".wma"};
    static constexpr std::array<std::string_view, 2> ebml_extensions{".mka", ".webm"};
    static constexpr std::array<std::string_view, 2> wav_extensions{".wav", ".wave"};
    static constexpr std::array<std::string_view, 1> flac_extensions{".flac"};
    static constexpr std::array<std::string_view, 2> mp2_extensions{".mp2", ".mpga"};
    static constexpr std::array<std::string_view, 1> mp3_extensions{".mp3"};
    static constexpr media::MetadataWriteCapabilities mp3_writes{
        .title = true,
        .artist = true,
        .album = true,
        .genre = true,
        .year = true,
        .comment = true,
        .composer = true,
        .track_number = true,
        .disc_number = true,
    };
    static const std::array formats{
        Format{"Ogg Vorbis/Opus", ogg_markers, ogg_extensions, {}},
        Format{"MP4/M4A AAC-LC/ALAC", mp4_markers, mp4_extensions, {}},
        Format{"ADTS AAC-LC", std::span<const Marker>{}, adts_extensions, {}},
        Format{"AIFF PCM", aiff_markers, aiff_extensions, {}},
        Format{"AIFC PCM", aifc_markers, aifc_extensions, {}},
        Format{"AU PCM", au_markers, au_extensions, {}},
        Format{"CAF PCM", caf_markers, caf_extensions, {}},
        Format{"Wave64 PCM", w64_markers, w64_extensions, {}},
        Format{"RF64 PCM", rf64_markers, rf64_extensions, {}},
        Format{"WavPack", wavpack_markers, wavpack_extensions, {}},
        Format{"Monkey's Audio", ape_markers, ape_extensions, {}},
        Format{"TTA", tta_markers, tta_extensions, {}},
        Format{"ASF WMAv2", asf_markers, asf_extensions, {}},
        Format{"Matroska/WebM Vorbis/Opus", ebml_markers, ebml_extensions, {}},
        Format{"WAV PCM", wav_markers, wav_extensions, {}},
        Format{"FLAC", flac_markers, flac_extensions, {}},
        Format{"MPEG audio Layer II", std::span<const Marker>{}, mp2_extensions, {}},
        Format{"MPEG audio Layer III", std::span<const Marker>{}, mp3_extensions, {}, {},
               mp3_writes}
    };
    return Backend{"FFmpeg", formats,
        [](const std::filesystem::path& path, const media::OperationContext& context) -> Opened {
            auto decoder{FfmpegDecoder::open(path, {}, context)};
            if (!decoder)
                return media::unexpected{decoder.error()};
            return std::unique_ptr<Decoder>{std::move(*decoder)};
        }};
}

FfmpegDecoder::FfmpegDecoder(std::filesystem::path path, std::size_t stream_index,
    DecoderStreamInfo info, std::unique_ptr<FfmpegInput> input,
    std::unique_ptr<PacketDecoder> packets, FfmpegDecoderOptions options) noexcept
    : Decoder{info}
    , path_{std::move(path)}
    , stream_index_{stream_index}
    , options_{options}
    , input_{std::move(input)}
    , packets_{std::move(packets)}
    , converter_{std::make_unique<PcmConverter>(info.spec, options.conversion)}
{
}

FfmpegDecoder::~FfmpegDecoder() = default;

media::Result<std::unique_ptr<FfmpegDecoder>> FfmpegDecoder::open(const std::filesystem::path& path,
    FfmpegDecoderOptions options, const media::OperationContext& context)
{
    auto stream{media::LocalFileStream::open(path, context)};

    if (!stream)
        return media::unexpected{stream.error()};

    auto input{FfmpegInput::open(std::move(*stream), context)};

    if (!input)
        return media::unexpected{input.error()};

    auto facts{(*input)->inspect(options.inspect)};

    if (!facts)
        return media::unexpected{facts.error()};

    auto packets{PacketDecoder::open(**input, *facts, options.packets)};

    if (!packets)
        return media::unexpected{packets.error()};

    return std::unique_ptr<FfmpegDecoder>{
        new FfmpegDecoder{path, facts->stream_index, facts->output, std::move(*input),
                          std::move(*packets), options}
    };
}

media::Result<bool> FfmpegDecoder::trim_frame(const AVFrame& frame,
    std::optional<pcm::Frames> preroll, std::int64_t timestamp_offset,
    std::optional<std::int64_t> sequential_start)
{
    const pcm::Frames reference{preroll.value_or(position_frames())};
    std::optional<std::int64_t> start;
    bool link_changed{false};

    if (preroll) {
        if (sequential_start)
            start = *sequential_start;
        else
            start = frame_position(frame, *input_->format(), stream_index_,
                static_cast<int>(spec().sample_rate.hz()));

        if (start && !sequential_start)
            *start -= timestamp_offset;
    } else {
        const auto position{frame_position(frame, *input_->format(), stream_index_,
            static_cast<int>(spec().sample_rate.hz()))};

        if (position) {
            if (!audible_timestamp_origin_)
                audible_timestamp_origin_ = *position;

            const std::int64_t relative{*position - *audible_timestamp_origin_};

            if (previous_relative_position_ && relative < *previous_relative_position_) {
                link_base_ = position_frames();
                link_changed = true;
            }

            start = static_cast<std::int64_t>(link_base_.count()) + relative;

            if (link_changed && frame.nb_samples > fresh_frame_samples_)
                *start -= frame.nb_samples - fresh_frame_samples_;

            previous_relative_position_ = relative;
        }
    }

    if (start) {
        const std::int64_t end{*start + frame.nb_samples};

        if (end <= static_cast<std::int64_t>(reference.count()))
            return false;
    }

    auto submitted{converter_->submit(frame)};

    if (!submitted)
        return media::unexpected{submitted.error()};

    if ((preroll || link_changed) && start &&
        *start < static_cast<std::int64_t>(reference.count())) {
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

    for (int work{0}; work < options_.decode_work_budget; ++work) {
        if (input_->context().stop_reason() != media::StopReason::none)
            return media::unexpected{input_->context().error()};

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

    auto probe{FfmpegDecoder::open(path_, options_, input_->context())};

    if (!probe)
        return media::unexpected{probe.error()};

    auto first{(*probe)->packets_->next_frame()};

    if (!first)
        return media::unexpected{first.error()};

    if (first->end_of_stream)
        return media::unexpected{seek_error("seek target is beyond actual audio")};

    const int initial_frame_samples{first->frame->nb_samples};
    const auto initial_position{frame_position(*first->frame, *input_->format(), stream_index_,
        static_cast<int>(spec().sample_rate.hz()))};

    audible_timestamp_origin_ = initial_position;
    fresh_frame_samples_ = initial_frame_samples;

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

    std::optional<std::int64_t> sequential_position;

    if (stream->codecpar->codec_id == AV_CODEC_ID_MP3)
        sequential_position = 0;

    if (!sequential_position && demux_seek(timestamp) < 0) {
        if (demux_seek(origin) < 0)
            return media::unexpected{seek_error("FFmpeg demuxer rejected the seek")};

        sequential_position = 0;
    }

    packets_->reset_after_seek();
    converter_->reset();
    source_ended_ = false;

    if (length_accuracy() == LengthAccuracy::exact && target == num_frames())
        return target;

    for (int work{0}; work < options_.seek_work_budget; ++work) {
        if (input_->context().stop_reason() != media::StopReason::none)
            return media::unexpected{input_->context().error()};

        auto event{packets_->next_frame()};

        if (!event)
            return media::unexpected{event.error()};

        if (event->end_of_stream)
            return media::unexpected{seek_error("seek target is beyond actual audio")};

        if (sequential_position && *sequential_position == 0 &&
            event->frame->nb_samples > initial_frame_samples)
            sequential_position = initial_frame_samples - event->frame->nb_samples;

        if (!sequential_position && event->frame->best_effort_timestamp == AV_NOPTS_VALUE &&
            target != pcm::Frames{})
            return media::unexpected{seek_error("stream lacks sample-accurate seek timestamps")};

        const auto position{frame_position(*event->frame, *input_->format(), stream_index_,
            static_cast<int>(spec().sample_rate.hz()))};

        if (!sequential_position && position && *position > seek_sample) {
            if (demux_seek(origin) < 0)
                return media::unexpected{seek_error("FFmpeg demuxer rejected the seek restart")};

            packets_->reset_after_seek();
            converter_->reset();
            sequential_position = 0;

            continue;
        }

        auto trimmed{trim_frame(*event->frame, target, timestamp_offset, sequential_position)};

        if (!trimmed)
            return media::unexpected{trimmed.error()};

        if (sequential_position)
            *sequential_position += event->frame->nb_samples;

        if (*trimmed && converter_->pending())
            return target;
    }

    return media::unexpected{
        media::Error{.category = media::ErrorCategory::limit_exceeded,
                     .detail = "seek preroll exceeded its budget"}
    };
}

int FfmpegDecoder::demux_seek(std::int64_t timestamp) noexcept
{
    if (options_.seek)
        return options_.seek(input_->format(), static_cast<int>(stream_index_), timestamp,
            AVSEEK_FLAG_BACKWARD);

    return av_seek_frame(input_->format(), static_cast<int>(stream_index_), timestamp,
        AVSEEK_FLAG_BACKWARD);
}

media::Result<SeekOutcome> FfmpegDecoder::seek_frame(pcm::Frames frame_index)
{
    auto candidate{FfmpegDecoder::open(path_, options_, input_->context())};

    if (!candidate)
        return media::unexpected{candidate.error()};

    auto staged{(*candidate)->stage_seek(frame_index)};

    if (!staged)
        return media::unexpected{staged.error()};

    input_.swap((*candidate)->input_);
    packets_.swap((*candidate)->packets_);
    converter_.swap((*candidate)->converter_);

    audible_timestamp_origin_ = (*candidate)->audible_timestamp_origin_;
    fresh_frame_samples_ = (*candidate)->fresh_frame_samples_;
    previous_relative_position_.reset();
    link_base_ = frame_index;
    source_ended_ = false;

    return SeekOutcome{.target = frame_index, .achieved = *staged, .exact = true};
}

} // namespace wiola::codec::ffmpeg
