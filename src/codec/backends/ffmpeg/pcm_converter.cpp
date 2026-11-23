/**
 * @file
 * @brief Private bounded conversion from FFmpeg frames to interleaved float PCM.
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

#include "pcm_converter.hpp"

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

#include <algorithm>

namespace wiola::codec::ffmpeg {

namespace {

constexpr int max_converted_frames{65536};
constexpr int drain_frames{4096};

media::Error conversion_error(media::ErrorCategory category, const char* detail)
{
    return media::Error{.category = category, .detail = detail};
}

} // namespace

PcmConverter::PcmConverter(pcm::StreamSpec output, PcmConversionApi api) noexcept
    : output_{output}
    , api_{api}
{
}

PcmConverter::~PcmConverter()
{
    swr_free(&converter_);
    av_channel_layout_uninit(&input_layout_);
}

int PcmConverter::out_samples(int input_frames) const noexcept
{
    if (api_.out_samples)
        return api_.out_samples(converter_, input_frames);

    return swr_get_out_samples(converter_, input_frames);
}

int PcmConverter::convert(std::uint8_t** output, int output_frames,
    const std::uint8_t* const * input, int input_frames) noexcept
{
    if (api_.convert)
        return api_.convert(converter_, output, output_frames, input, input_frames);

    return swr_convert(converter_, output, output_frames, input, input_frames);
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

    if (output_.channel_layout == pcm::ChannelLayout::five_point_one_side)
        av_channel_layout_from_mask(&output_layout, AV_CH_LAYOUT_5POINT1);
    else
        av_channel_layout_default(&output_layout, static_cast<int>(output_.num_channels));

    const int allocated{swr_alloc_set_opts2(&converter_, &output_layout, AV_SAMPLE_FMT_FLT,
        frame.sample_rate, &input_layout_, static_cast<AVSampleFormat>(frame.format),
        frame.sample_rate, 0, nullptr)};

    av_channel_layout_uninit(&output_layout);

    if (allocated < 0 || !converter_)
        return media::unexpected{conversion_error(media::ErrorCategory::limit_exceeded,
            "PCM converter allocation failed")};

    const int initialized{api_.initialize ? api_.initialize(converter_) : swr_init(converter_)};

    if (initialized < 0)
        return media::unexpected{
            conversion_error(media::ErrorCategory::damaged, "PCM converter initialization failed")};

    input_format_ = frame.format;

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

    const int capacity{out_samples(frame.nb_samples)};

    if (capacity < 0 || capacity > max_converted_frames)
        return media::unexpected{conversion_error(media::ErrorCategory::limit_exceeded,
            "converted PCM exceeds the frame bound")};

    pending_.resize(output_.samples_per(pcm::Frames{static_cast<std::size_t>(capacity)}));

    std::uint8_t* output[]{reinterpret_cast<std::uint8_t*>(pending_.data())};
    const int produced{convert(output, capacity, frame.extended_data, frame.nb_samples)};

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

    std::vector<float> converted(output_.samples_per(pcm::Frames{drain_frames}));
    std::uint8_t* output[]{reinterpret_cast<std::uint8_t*>(converted.data())};

    for (int calls{0}; calls < max_converted_frames / drain_frames; ++calls) {
        const int produced{convert(output, drain_frames, nullptr, 0)};

        if (produced < 0)
            return media::unexpected{
                conversion_error(media::ErrorCategory::damaged, "PCM converter drain failed")};

        if (produced == 0) {
            finished_ = true;

            return {};
        }

        const auto first{converted.begin()};
        const auto last{first +
            static_cast<std::ptrdiff_t>(output_.samples_per(pcm::Frames{
                static_cast<std::size_t>(produced)}))};

        pending_.insert(pending_.end(), first, last);
    }

    return media::unexpected{conversion_error(media::ErrorCategory::limit_exceeded,
        "PCM converter drain did not finish")};
}

void PcmConverter::reset() noexcept
{
    swr_free(&converter_);
    av_channel_layout_uninit(&input_layout_);

    input_format_ = -1;
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

media::Result<media::DecodeOutcome> PcmConverter::read(std::span<float> output, bool source_ended)
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

    return media::DecodeOutcome{.frames = wanted,
        .end_of_stream = source_ended && finished_ && !pending()};
}

} // namespace wiola::codec::ffmpeg
