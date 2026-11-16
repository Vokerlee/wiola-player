/**
 * @file
 * @brief Private bounded conversion from FFmpeg frames to interleaved float PCM.
 * @author Roman Glaz
 * @copyright © 2026, <vokerlee@gmail.com>
 */

#include "pcm_converter.hpp"

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

#include <algorithm>
#include <limits>

namespace wiola::codec::ffmpeg {

namespace {

constexpr int max_converted_frames{65536};
constexpr int drain_frames{4096};

media::Error conversion_error(media::ErrorCategory category, const char* detail)
{
    return media::Error{.category = category, .detail = detail};
}

} // namespace

PcmConverter::PcmConverter(pcm::StreamSpec output) noexcept
    : output_{output}
{
}

PcmConverter::~PcmConverter()
{
    swr_free(&converter_);
    av_channel_layout_uninit(&input_layout_);
}

media::Result<void> PcmConverter::configure(const AVFrame& frame)
{
    const int channels{frame.ch_layout.nb_channels};
    if (frame.sample_rate != static_cast<int>(output_.sample_rate.hz()) || channels <= 0 ||
        static_cast<std::size_t>(channels) != output_.num_channels)
        return media::unexpected{conversion_error(media::ErrorCategory::unsupported,
            "decoded stream changed rate or channel count")};

    if (converter_) {
        if (frame.format != input_format_ ||
            av_channel_layout_compare(&input_layout_, &frame.ch_layout) != 0)
            return media::unexpected{conversion_error(media::ErrorCategory::unsupported,
                "decoded stream changed sample format or layout")};
        return {};
    }

    if (av_channel_layout_check(&frame.ch_layout))
        av_channel_layout_copy(&input_layout_, &frame.ch_layout);
    else
        av_channel_layout_default(&input_layout_, channels);
    AVChannelLayout output_layout{};
    av_channel_layout_default(&output_layout, static_cast<int>(output_.num_channels));
    const int allocated{swr_alloc_set_opts2(&converter_, &output_layout, AV_SAMPLE_FMT_FLT,
        frame.sample_rate, &input_layout_, static_cast<AVSampleFormat>(frame.format),
        frame.sample_rate, 0, nullptr)};
    av_channel_layout_uninit(&output_layout);
    if (allocated < 0 || !converter_)
        return media::unexpected{conversion_error(media::ErrorCategory::limit_exceeded,
            "PCM converter allocation failed")};
    if (swr_init(converter_) < 0)
        return media::unexpected{
            conversion_error(media::ErrorCategory::damaged, "PCM converter initialization failed")};

    input_format_ = frame.format;
    input_rate_ = frame.sample_rate;
    return {};
}

media::Result<void> PcmConverter::submit(const AVFrame& frame)
{
    if (pending() || finished_)
        return media::unexpected{conversion_error(media::ErrorCategory::damaged,
            "PCM converter submitted out of order")};
    if (frame.nb_samples < 0 || frame.nb_samples > max_converted_frames)
        return media::unexpected{conversion_error(media::ErrorCategory::limit_exceeded,
            "decoded frame exceeds the PCM bound")};

    auto configured{configure(frame)};
    if (!configured)
        return configured;

    const int capacity{swr_get_out_samples(converter_, frame.nb_samples)};
    if (capacity < 0 || capacity > max_converted_frames)
        return media::unexpected{conversion_error(media::ErrorCategory::limit_exceeded,
            "converted PCM exceeds the frame bound")};
    const auto samples{
        output_.checked_samples_per(pcm::Frames{static_cast<std::size_t>(capacity)})};
    if (!samples)
        return media::unexpected{conversion_error(media::ErrorCategory::limit_exceeded,
            "converted PCM sample count overflow")};

    pending_.resize(*samples);
    uint8_t* output[]{reinterpret_cast<uint8_t*>(pending_.data())};
    const int produced{
        swr_convert(converter_, output, capacity, frame.extended_data, frame.nb_samples)};
    if (produced < 0)
        return media::unexpected{
            conversion_error(media::ErrorCategory::damaged, "PCM sample conversion failed")};
    pending_.resize(output_.samples_per(pcm::Frames{static_cast<std::size_t>(produced)}));
    offset_ = 0;
    return {};
}

media::Result<void> PcmConverter::finish()
{
    if (finished_)
        return {};
    if (pending())
        return media::unexpected{conversion_error(media::ErrorCategory::damaged,
            "PCM converter drained before pending audio was read")};
    if (!converter_) {
        finished_ = true;
        return {};
    }

    const auto samples{output_.checked_samples_per(pcm::Frames{drain_frames})};
    if (!samples)
        return media::unexpected{
            conversion_error(media::ErrorCategory::limit_exceeded, "PCM drain size overflow")};
    std::vector<float> converted(*samples);
    uint8_t* output[]{reinterpret_cast<uint8_t*>(converted.data())};
    for (int calls{0}; calls < max_converted_frames / drain_frames; ++calls) {
        const int produced{swr_convert(converter_, output, drain_frames, nullptr, 0)};
        if (produced < 0)
            return media::unexpected{
                conversion_error(media::ErrorCategory::damaged, "PCM converter drain failed")};
        if (produced == 0) {
            finished_ = true;
            return {};
        }
        if (pending_.size() + output_.samples_per(pcm::Frames{static_cast<std::size_t>(produced)}) >
            output_.samples_per(pcm::Frames{max_converted_frames}))
            return media::unexpected{conversion_error(media::ErrorCategory::limit_exceeded,
                "PCM drain exceeds the pending bound")};
        pending_.insert(pending_.end(), converted.begin(),
            converted.begin() +
                static_cast<std::ptrdiff_t>(output_.samples_per(pcm::Frames{
                    static_cast<std::size_t>(produced)})));
    }
    return media::unexpected{conversion_error(media::ErrorCategory::limit_exceeded,
        "PCM converter drain did not finish")};
}

void PcmConverter::reset() noexcept
{
    swr_free(&converter_);
    av_channel_layout_uninit(&input_layout_);
    input_format_ = -1;
    input_rate_ = 0;
    pending_.clear();
    offset_ = 0;
    finished_ = false;
}

void PcmConverter::discard_first(pcm::Frames frames) noexcept
{
    const std::size_t samples{std::min(output_.samples_per(frames), pending_.size() - offset_)};
    offset_ += samples;
    if (!pending()) {
        pending_.clear();
        offset_ = 0;
    }
}

void PcmConverter::truncate(pcm::Frames frames) noexcept
{
    const std::size_t samples{output_.samples_per(frames)};
    pending_.resize(offset_ + std::min(samples, pending_.size() - offset_));
}

media::Result<DecodeOutcome> PcmConverter::read(std::span<float> output, bool source_ended)
{
    const pcm::Frames available{output_.frames_per(pending_.size() - offset_)};
    const pcm::Frames wanted{std::min(available, output_.frames_per(output.size()))};
    const std::size_t samples{output_.samples_per(wanted)};
    std::copy_n(pending_.begin() + static_cast<std::ptrdiff_t>(offset_), samples, output.begin());
    offset_ += samples;
    if (!pending()) {
        pending_.clear();
        offset_ = 0;
    }
    return DecodeOutcome{.frames = wanted,
        .end_of_stream = source_ended && finished_ && !pending()};
}

} // namespace wiola::codec::ffmpeg
