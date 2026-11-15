/**
 * @file
 * @brief Private FFmpeg input ownership over an independent ByteStream.
 * @author Roman Glaz
 * @copyright © 2026, <vokerlee@gmail.com>
 */

#include "input.hpp"

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/mem.h>
}

#include <cerrno>
#include <cstdio>
#include <limits>

namespace wiola::codec::ffmpeg {

namespace {

constexpr int io_buffer_size{16 * 1024};
constexpr std::int64_t probe_bytes{256 * 1024};
constexpr std::int64_t probe_time{1000000};

media::Error failure(media::ErrorCategory category, const char* detail)
{
    return media::Error{.category = category, .detail = detail};
}

bool has_exact_sample_count(AVCodecID codec)
{
    switch (codec) {
    case AV_CODEC_ID_ALAC:
    case AV_CODEC_ID_FLAC:
    case AV_CODEC_ID_PCM_ALAW:
    case AV_CODEC_ID_PCM_MULAW:
    case AV_CODEC_ID_PCM_S8:
    case AV_CODEC_ID_PCM_U8:
    case AV_CODEC_ID_PCM_S16LE:
    case AV_CODEC_ID_PCM_S16BE:
    case AV_CODEC_ID_PCM_S24LE:
    case AV_CODEC_ID_PCM_S24BE:
    case AV_CODEC_ID_PCM_S32LE:
    case AV_CODEC_ID_PCM_S32BE:
    case AV_CODEC_ID_PCM_F32LE:
    case AV_CODEC_ID_PCM_F32BE:
    case AV_CODEC_ID_PCM_F64LE:
    case AV_CODEC_ID_PCM_F64BE:
        return true;
    default:
        return false;
    }
}

} // namespace

FfmpegInput::FfmpegInput(std::unique_ptr<media::ByteStream> stream) noexcept
    : stream_{std::move(stream)}
{
}

media::Result<std::unique_ptr<FfmpegInput>>
FfmpegInput::open(std::unique_ptr<media::ByteStream> stream)
{
    if (!stream)
        return media::unexpected{failure(media::ErrorCategory::unreadable, "missing byte stream")};

    auto input{std::unique_ptr<FfmpegInput>{new FfmpegInput{std::move(stream)}}};
    auto initialized{input->initialize()};
    if (!initialized)
        return media::unexpected{initialized.error()};
    return input;
}

media::Result<void> FfmpegInput::initialize()
{
    auto measured{measure_size()};
    if (!measured)
        return measured;

    format_ = avformat_alloc_context();
    if (!format_)
        return media::unexpected{
            failure(media::ErrorCategory::unreadable, "FFmpeg input allocation failed")};

    auto* buffer{static_cast<unsigned char*>(av_malloc(io_buffer_size))};
    if (!buffer)
        return media::unexpected{
            failure(media::ErrorCategory::unreadable, "FFmpeg I/O allocation failed")};

    io_ = avio_alloc_context(buffer, io_buffer_size, 0, this, &FfmpegInput::read, nullptr,
        &FfmpegInput::seek);
    if (!io_) {
        av_free(buffer);
        return media::unexpected{
            failure(media::ErrorCategory::unreadable, "FFmpeg I/O allocation failed")};
    }

    format_->pb = static_cast<AVIOContext*>(io_);
    format_->flags |= AVFMT_FLAG_CUSTOM_IO;
    format_->probesize = probe_bytes;
    format_->max_analyze_duration = probe_time;
    format_->io_open = &FfmpegInput::reject_secondary_open;

    const int opened{avformat_open_input(&format_, "", nullptr, nullptr)};
    if (opened < 0) {
        if (io_error_)
            return media::unexpected{*io_error_};
        return media::unexpected{
            failure(media::ErrorCategory::not_mine, "FFmpeg did not recognize the input")};
    }

    return {};
}

media::Result<void> FfmpegInput::measure_size()
{
    auto original{stream_->tell()};
    if (!original)
        return media::unexpected{original.error()};
    auto end{stream_->seek(0, media::SeekOrigin::end)};
    if (!end)
        return media::unexpected{end.error()};
    auto measured{stream_->tell()};
    if (!measured)
        return media::unexpected{measured.error()};
    auto restored{stream_->seek(static_cast<std::int64_t>(*original), media::SeekOrigin::start)};
    if (!restored)
        return media::unexpected{restored.error()};
    if (*measured > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
        return media::unexpected{
            failure(media::ErrorCategory::seek_failed, "input exceeds FFmpeg offsets")};

    size_ = *measured;
    return {};
}

FfmpegInput::~FfmpegInput()
{
    if (format_)
        avformat_close_input(&format_);

    if (io_) {
        auto* context{static_cast<AVIOContext*>(io_)};
        av_freep(&context->buffer);
        avio_context_free(&context);
    }
}

media::Result<FfmpegStreamFacts> FfmpegInput::inspect()
{
    if (avformat_find_stream_info(format_, nullptr) < 0) {
        if (io_error_)
            return media::unexpected{*io_error_};
        return media::unexpected{
            failure(media::ErrorCategory::damaged, "FFmpeg stream inspection failed")};
    }

    AVStream* selected{nullptr};
    bool saw_audio{false};
    for (unsigned int index{0}; index < format_->nb_streams; ++index) {
        AVStream* candidate{format_->streams[index]};
        const AVCodecParameters* parameters{candidate->codecpar};
        if (parameters->codec_type != AVMEDIA_TYPE_AUDIO)
            continue;
        saw_audio = true;
        if (!avcodec_find_decoder(parameters->codec_id) || parameters->ch_layout.nb_channels <= 0 ||
            parameters->ch_layout.nb_channels > 2 || parameters->sample_rate <= 0)
            continue;
        if (!selected ||
            (!(selected->disposition & AV_DISPOSITION_DEFAULT) &&
                (candidate->disposition & AV_DISPOSITION_DEFAULT)))
            selected = candidate;
    }

    if (!selected)
        return media::unexpected{failure(media::ErrorCategory::unsupported,
            saw_audio ? "no supported mono/stereo audio stream"
                      : "input contains no audio stream")};

    const AVCodecParameters* parameters{selected->codecpar};
    pcm::StreamSpec spec{.sample_rate = units::Frequency{parameters->codec_id == AV_CODEC_ID_OPUS
                                 ? 48000.0
                                 : static_cast<double>(parameters->sample_rate)},
        .num_channels = static_cast<std::size_t>(parameters->ch_layout.nb_channels)};
    std::optional<pcm::Frames> length;
    AVRational time_base{selected->time_base};
    std::int64_t duration{selected->duration};
    if (duration == AV_NOPTS_VALUE && format_->duration != AV_NOPTS_VALUE) {
        duration = format_->duration;
        time_base = AV_TIME_BASE_Q;
    }
    if (duration != AV_NOPTS_VALUE && duration >= 0 && av_q2d(time_base) > 0.0) {
        const double seconds{static_cast<double>(duration) * av_q2d(time_base)};
        length = spec.checked_frames_at(units::Time{seconds});
    }

    return FfmpegStreamFacts{
        .stream_index = static_cast<std::size_t>(selected->index),
        .output = DecoderStreamInfo{.spec = spec,
                                    .length = length,
                                    .length_accuracy = length
                ? (has_exact_sample_count(parameters->codec_id) ? LengthAccuracy::exact
                                                                : LengthAccuracy::estimated)
                : LengthAccuracy::unknown,
                                    .seekable = format_->pb->seekable != 0}
    };
}

int FfmpegInput::read(void* opaque, unsigned char* output, int size) noexcept
{
    return static_cast<FfmpegInput*>(opaque)->read(output, size);
}

int FfmpegInput::read(unsigned char* output, int size) noexcept
{
    if (!output || size < 0)
        return AVERROR(EINVAL);

    try {
        auto count{
            stream_->read({reinterpret_cast<std::byte*>(output), static_cast<std::size_t>(size)})};
        if (!count) {
            if (!io_error_)
                io_error_ = count.error();
            return AVERROR(EIO);
        }
        return *count == 0 ? AVERROR_EOF : static_cast<int>(*count);
    } catch (...) {
        io_error_ =
            failure(media::ErrorCategory::read_failed, "byte stream read threw an exception");
        return AVERROR(EIO);
    }
}

std::int64_t FfmpegInput::seek(void* opaque, std::int64_t offset, int origin) noexcept
{
    return static_cast<FfmpegInput*>(opaque)->seek(offset, origin);
}

std::int64_t FfmpegInput::seek(std::int64_t offset, int origin) noexcept
{
    if (origin & AVSEEK_SIZE)
        return static_cast<std::int64_t>(size_);

    origin &= ~AVSEEK_FORCE;
    const std::optional<media::SeekOrigin> mapped{origin == SEEK_SET
            ? std::optional{media::SeekOrigin::start}
            : origin == SEEK_CUR ? std::optional{media::SeekOrigin::current}
            : origin == SEEK_END ? std::optional{media::SeekOrigin::end}
                                 : std::nullopt};
    if (!mapped)
        return AVERROR(EINVAL);

    try {
        auto moved{stream_->seek(offset, *mapped)};
        if (!moved) {
            io_error_ = moved.error();
            return AVERROR(EIO);
        }
        auto position{stream_->tell()};
        if (!position) {
            io_error_ = position.error();
            return AVERROR(EIO);
        }
        if (*position > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
            return AVERROR(EOVERFLOW);
        return static_cast<std::int64_t>(*position);
    } catch (...) {
        io_error_ =
            failure(media::ErrorCategory::seek_failed, "byte stream seek threw an exception");
        return AVERROR(EIO);
    }
}

int FfmpegInput::reject_secondary_open(AVFormatContext*, AVIOContext**, const char*, int,
    AVDictionary**) noexcept
{
    return AVERROR(EACCES);
}

} // namespace wiola::codec::ffmpeg
