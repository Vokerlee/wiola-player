/**
 * @file
 * @brief FFmpeg PCM conversion and neutral decoder adapter tests.
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

#include <media/codecs/ffmpeg/decoder.hpp>
#include <media/codecs/ffmpeg/pcm_converter.hpp>

#include <gtest/gtest.h>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/mem.h>
#include <libswresample/swresample.h>
}

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <vector>

namespace {

class TestFrame final {
public:
    TestFrame(AVSampleFormat format, int rate, int channels, int samples)
        : frame_{av_frame_alloc()}
    {
        frame_->format = format;
        frame_->sample_rate = rate;
        frame_->nb_samples = samples;
        av_channel_layout_default(&frame_->ch_layout, channels);
        if (av_frame_get_buffer(frame_, 0) < 0)
            throw std::bad_alloc{};
    }

    ~TestFrame() { av_frame_free(&frame_); }

    AVFrame* get() const noexcept { return frame_; }

    void set_layout(std::uint64_t mask) noexcept
    {
        av_channel_layout_uninit(&frame_->ch_layout);
        av_channel_layout_from_mask(&frame_->ch_layout, mask);
    }

private:
    AVFrame* frame_;
};

TEST(FfmpegPcm, ConvertsPackedIntegersAcrossTinyReads)
{
    TestFrame frame{AV_SAMPLE_FMT_S16, 44100, 2, 2};
    auto* samples{reinterpret_cast<std::int16_t*>(frame.get()->data[0])};
    samples[0] = 0;
    samples[1] = 32767;
    samples[2] = -32768;
    samples[3] = 16384;
    wiola::codec::ffmpeg::PcmConverter converter{
        {wiola::units::Frequency{44100.0}, 2}
    };
    ASSERT_TRUE(converter.submit(*frame.get()));
    std::array<float, 2> output{};

    auto first{converter.read(output, false)};
    auto second{converter.read(output, false)};

    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    EXPECT_EQ(first->frames, wiola::pcm::Frames{1});
    EXPECT_EQ(second->frames, wiola::pcm::Frames{1});
    EXPECT_FLOAT_EQ(output[0], -1.0F);
    EXPECT_FLOAT_EQ(output[1], 0.5F);
    EXPECT_FALSE(converter.pending());
}

TEST(FfmpegPcm, ConvertsPlanarFloatWithoutChangingLayout)
{
    TestFrame frame{AV_SAMPLE_FMT_FLTP, 48000, 2, 2};
    auto* left{reinterpret_cast<float*>(frame.get()->data[0])};
    auto* right{reinterpret_cast<float*>(frame.get()->data[1])};
    left[0] = 0.25F;
    left[1] = -0.5F;
    right[0] = 0.75F;
    right[1] = -1.0F;
    wiola::codec::ffmpeg::PcmConverter converter{
        {wiola::units::Frequency{48000.0}, 2}
    };
    ASSERT_TRUE(converter.submit(*frame.get()));
    std::array<float, 4> output{};

    auto converted{converter.read(output, false)};

    ASSERT_TRUE(converted);
    EXPECT_EQ(converted->frames, wiola::pcm::Frames{2});
    EXPECT_EQ(output, (std::array<float, 4>{0.25F, 0.75F, -0.5F, -1.0F}));
}

TEST(FfmpegPcm, RetainsALargeFrameAcrossSmallSpans)
{
    constexpr int num_frames{4096};
    TestFrame frame{AV_SAMPLE_FMT_S16, 44100, 1, num_frames};
    auto* samples{reinterpret_cast<std::int16_t*>(frame.get()->data[0])};
    for (int index{0}; index < num_frames; ++index)
        samples[index] = static_cast<std::int16_t>(index % 32767);
    wiola::codec::ffmpeg::PcmConverter converter{
        {wiola::units::Frequency{44100.0}, 1}
    };
    ASSERT_TRUE(converter.submit(*frame.get()));
    std::array<float, 2> output{};
    std::size_t total{0};
    while (converter.pending()) {
        auto converted{converter.read(output, false)};
        ASSERT_TRUE(converted);
        total += converted->frames.count();
    }
    EXPECT_EQ(total, static_cast<std::size_t>(num_frames));
}

TEST(FfmpegPcm, DrainsAndRejectsRateChangesBeforePublishingPcm)
{
    TestFrame frame{AV_SAMPLE_FMT_S16, 44100, 2, 1};
    wiola::codec::ffmpeg::PcmConverter converter{
        {wiola::units::Frequency{44100.0}, 2}
    };
    ASSERT_TRUE(converter.submit(*frame.get()));
    std::array<float, 2> output{};
    ASSERT_TRUE(converter.read(output, false));

    TestFrame changed{AV_SAMPLE_FMT_S16, 48000, 2, 1};
    auto rejected{converter.submit(*changed.get())};
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().category, wiola::media::ErrorCategory::unsupported);

    ASSERT_TRUE(converter.finish());
    auto end{converter.read(output, true)};
    ASSERT_TRUE(end);
    EXPECT_TRUE(end->end_of_stream);
}

TEST(FfmpegPcm, RejectsSampleFormatAndSemanticLayoutChanges)
{
    wiola::codec::ffmpeg::PcmConverter converter{
        {wiola::units::Frequency{44100.0}, 6, wiola::pcm::ChannelLayout::five_point_one_side}
    };
    TestFrame initial{AV_SAMPLE_FMT_S16, 44100, 6, 1};
    initial.set_layout(AV_CH_LAYOUT_5POINT1);
    ASSERT_TRUE(converter.submit(*initial.get()));
    std::array<float, 6> output{};
    ASSERT_TRUE(converter.read(output, false));

    TestFrame changed_format{AV_SAMPLE_FMT_S32, 44100, 6, 1};
    auto format_result{converter.submit(*changed_format.get())};
    ASSERT_FALSE(format_result);
    EXPECT_EQ(format_result.error().category, wiola::media::ErrorCategory::unsupported);

    TestFrame changed_layout{AV_SAMPLE_FMT_S16, 44100, 6, 1};
    changed_layout.set_layout(AV_CH_LAYOUT_5POINT1_BACK);
    auto layout_result{converter.submit(*changed_layout.get())};
    ASSERT_FALSE(layout_result);
    EXPECT_EQ(layout_result.error().category, wiola::media::ErrorCategory::unsupported);
}

class AllocationLimit final {
public:
    explicit AllocationLimit(std::size_t bytes) { av_max_alloc(bytes); }

    ~AllocationLimit() { av_max_alloc(std::numeric_limits<std::size_t>::max()); }
};

TEST(FfmpegPcm, RejectsSubmissionAndDrainWhilePcmIsPending)
{
    TestFrame frame{AV_SAMPLE_FMT_S16, 44100, 2, 1};
    wiola::codec::ffmpeg::PcmConverter converter{
        {wiola::units::Frequency{44100.0}, 2}
    };
    ASSERT_TRUE(converter.submit(*frame.get()));

    auto repeated{converter.submit(*frame.get())};
    auto drained{converter.finish()};

    ASSERT_FALSE(repeated);
    ASSERT_FALSE(drained);
    EXPECT_EQ(repeated.error().category, wiola::media::ErrorCategory::damaged);
    EXPECT_EQ(drained.error().category, wiola::media::ErrorCategory::damaged);
}

TEST(FfmpegPcm, RejectsAFrameBeyondTheConversionBound)
{
    TestFrame frame{AV_SAMPLE_FMT_S16, 44100, 2, 1};
    frame.get()->nb_samples = 65537;
    wiola::codec::ffmpeg::PcmConverter converter{
        {wiola::units::Frequency{44100.0}, 2}
    };

    auto submitted{converter.submit(*frame.get())};

    ASSERT_FALSE(submitted);
    EXPECT_EQ(submitted.error().category, wiola::media::ErrorCategory::limit_exceeded);
}

TEST(FfmpegPcm, UsesTheDefaultLayoutWhenAFrameHasNoExplicitMapping)
{
    TestFrame frame{AV_SAMPLE_FMT_S16, 44100, 2, 1};
    av_channel_layout_uninit(&frame.get()->ch_layout);
    frame.get()->ch_layout.order = AV_CHANNEL_ORDER_NATIVE;
    frame.get()->ch_layout.nb_channels = 2;
    frame.get()->ch_layout.u.mask = 0;
    wiola::codec::ffmpeg::PcmConverter converter{
        {wiola::units::Frequency{44100.0}, 2}
    };

    auto submitted{converter.submit(*frame.get())};

    ASSERT_TRUE(submitted);
    EXPECT_TRUE(converter.pending());
}

TEST(FfmpegPcm, ReportsConverterAllocationFailure)
{
    TestFrame frame{AV_SAMPLE_FMT_S16, 44100, 2, 1};
    wiola::codec::ffmpeg::PcmConverter converter{
        {wiola::units::Frequency{44100.0}, 2}
    };
    AllocationLimit limited{1};

    auto submitted{converter.submit(*frame.get())};

    ASSERT_FALSE(submitted);
    EXPECT_EQ(submitted.error().category, wiola::media::ErrorCategory::limit_exceeded);
}

TEST(FfmpegPcm, ReportsConverterInitializationFailure)
{
    TestFrame frame{AV_SAMPLE_FMT_S16, 44100, 2, 1};
    wiola::codec::ffmpeg::PcmConversionApi api;
    api.initialize = [](SwrContext*) {
        return -1;
    };
    wiola::codec::ffmpeg::PcmConverter converter{
        {wiola::units::Frequency{44100.0}, 2},
        api
    };

    auto submitted{converter.submit(*frame.get())};

    ASSERT_FALSE(submitted);
    EXPECT_EQ(submitted.error().category, wiola::media::ErrorCategory::damaged);
}

TEST(FfmpegPcm, RejectsAnInvalidConvertedFrameCapacity)
{
    TestFrame frame{AV_SAMPLE_FMT_S16, 44100, 2, 1};
    wiola::codec::ffmpeg::PcmConversionApi api;
    api.out_samples = [](SwrContext*, int) {
        return -1;
    };
    wiola::codec::ffmpeg::PcmConverter converter{
        {wiola::units::Frequency{44100.0}, 2},
        api
    };

    auto submitted{converter.submit(*frame.get())};

    ASSERT_FALSE(submitted);
    EXPECT_EQ(submitted.error().category, wiola::media::ErrorCategory::limit_exceeded);
}

TEST(FfmpegPcm, ReportsSampleConversionFailure)
{
    TestFrame frame{AV_SAMPLE_FMT_S16, 44100, 2, 1};
    wiola::codec::ffmpeg::PcmConversionApi api;
    api.convert = [](SwrContext*, std::uint8_t**, int, const std::uint8_t* const *, int) {
        return -1;
    };
    wiola::codec::ffmpeg::PcmConverter converter{
        {wiola::units::Frequency{44100.0}, 2},
        api
    };

    auto submitted{converter.submit(*frame.get())};

    ASSERT_FALSE(submitted);
    EXPECT_EQ(submitted.error().category, wiola::media::ErrorCategory::damaged);
}

TEST(FfmpegPcm, FinishesImmediatelyWithoutAConverter)
{
    wiola::codec::ffmpeg::PcmConverter converter{
        {wiola::units::Frequency{44100.0}, 2}
    };

    auto first{converter.finish()};
    auto repeated{converter.finish()};

    ASSERT_TRUE(first);
    ASSERT_TRUE(repeated);
}

TEST(FfmpegPcm, ReportsConverterDrainFailure)
{
    TestFrame frame{AV_SAMPLE_FMT_S16, 44100, 2, 1};
    wiola::codec::ffmpeg::PcmConversionApi api;
    api.convert = [](SwrContext*, std::uint8_t**, int, const std::uint8_t* const * input, int) {
        return input ? 1 : -1;
    };
    wiola::codec::ffmpeg::PcmConverter converter{
        {wiola::units::Frequency{44100.0}, 2},
        api
    };
    ASSERT_TRUE(converter.submit(*frame.get()));
    std::array<float, 2> output{};
    ASSERT_TRUE(converter.read(output, false));

    auto drained{converter.finish()};

    ASSERT_FALSE(drained);
    EXPECT_EQ(drained.error().category, wiola::media::ErrorCategory::damaged);
}

TEST(FfmpegPcm, BoundsAConverterThatNeverFinishesDraining)
{
    TestFrame frame{AV_SAMPLE_FMT_S16, 44100, 2, 1};
    wiola::codec::ffmpeg::PcmConversionApi api;
    api.convert = [](SwrContext*, std::uint8_t**, int, const std::uint8_t* const *, int) {
        return 1;
    };
    wiola::codec::ffmpeg::PcmConverter converter{
        {wiola::units::Frequency{44100.0}, 2},
        api
    };
    ASSERT_TRUE(converter.submit(*frame.get()));
    std::array<float, 2> output{};
    ASSERT_TRUE(converter.read(output, false));

    auto drained{converter.finish()};

    ASSERT_FALSE(drained);
    EXPECT_EQ(drained.error().category, wiola::media::ErrorCategory::limit_exceeded);
}

TEST(FfmpegPcm, ResetDropsPendingAudioAndAllowsACleanFinish)
{
    TestFrame frame{AV_SAMPLE_FMT_S16, 44100, 2, 1};
    wiola::codec::ffmpeg::PcmConverter converter{
        {wiola::units::Frequency{44100.0}, 2}
    };
    ASSERT_TRUE(converter.submit(*frame.get()));

    converter.reset();
    auto drained{converter.finish()};

    EXPECT_FALSE(converter.pending());
    ASSERT_TRUE(drained);
}

TEST(FfmpegPcm, DiscardFirstClearsTheConsumedBuffer)
{
    TestFrame frame{AV_SAMPLE_FMT_S16, 44100, 2, 2};
    wiola::codec::ffmpeg::PcmConverter converter{
        {wiola::units::Frequency{44100.0}, 2}
    };
    ASSERT_TRUE(converter.submit(*frame.get()));

    converter.discard_first(wiola::pcm::Frames{2});

    EXPECT_FALSE(converter.pending());
}

wiola::media::Result<std::vector<float>> decode_all(wiola::media::AudioDecoder& decoder)
{
    std::vector<float> samples;
    std::array<float, 14> block{};
    for (int calls{0}; calls < 10000; ++calls) {
        auto decoded{decoder.read(block)};
        if (!decoded)
            return wiola::media::unexpected{decoded.error()};
        const std::size_t count{decoder.spec().samples_per(decoded->frames)};
        samples.insert(samples.end(), block.begin(),
            block.begin() + static_cast<std::ptrdiff_t>(count));
        if (decoded->end_of_stream)
            return samples;
    }
    return wiola::media::unexpected{
        wiola::media::Error{.category = wiola::media::ErrorCategory::limit_exceeded,
                            .detail = "decode did not finish"}
    };
}

TEST(FfmpegDecoder, MatchesAnIndependentlyEncodedPcmFixtureWithTinyBlocks)
{
    const std::filesystem::path path{std::filesystem::path{WIOLA_TEST_DATA_DIR} / "tone.wav"};
    auto broad{wiola::codec::ffmpeg::FfmpegDecoder::open(path)};
    auto reference{wiola::codec::ffmpeg::FfmpegDecoder::open(
        std::filesystem::path{WIOLA_TEST_DATA_DIR} / "tone-s16be.aiff"
    )};
    ASSERT_TRUE(broad);
    ASSERT_TRUE(reference);

    auto actual{decode_all(**broad)};
    auto expected{decode_all(**reference)};

    ASSERT_TRUE(actual);
    ASSERT_TRUE(expected);
    EXPECT_EQ(*actual, *expected);
    EXPECT_EQ(actual->size(), 11025U * 2U);
}

TEST(FfmpegDecoder, EmitsAndDrainsDelayedAacPcm)
{
    auto decoder{wiola::codec::ffmpeg::FfmpegDecoder::open(
        std::filesystem::path{WIOLA_TEST_DATA_DIR} / "multi-audio.m4a"
    )};
    ASSERT_TRUE(decoder);

    auto samples{decode_all(**decoder)};

    ASSERT_TRUE(samples);
    EXPECT_FALSE(samples->empty());
}

TEST(FfmpegDecoder, SampleAccurateWavSeekMatchesSequentialDecoding)
{
    const std::filesystem::path path{std::filesystem::path{WIOLA_TEST_DATA_DIR} / "tone.wav"};
    auto reference{wiola::codec::ffmpeg::FfmpegDecoder::open(path)};
    auto decoder{wiola::codec::ffmpeg::FfmpegDecoder::open(path)};
    ASSERT_TRUE(reference);
    ASSERT_TRUE(decoder);
    auto expected{decode_all(**reference)};
    ASSERT_TRUE(expected);

    auto sought{(*decoder)->seek(wiola::pcm::Frames{4000})};
    ASSERT_TRUE(sought);
    EXPECT_EQ(sought->achieved, wiola::pcm::Frames{4000});
    EXPECT_TRUE(sought->exact);
    std::array<float, 128> output{};
    auto actual{(*decoder)->read(output)};
    ASSERT_TRUE(actual);
    ASSERT_EQ(actual->frames, wiola::pcm::Frames{64});
    EXPECT_TRUE(std::equal(output.begin(), output.end(), expected->begin() + 4000 * 2));
}

TEST(FfmpegDecoder, FailedSeekPreservesPositionAndSubsequentSamples)
{
    const std::filesystem::path path{std::filesystem::path{WIOLA_TEST_DATA_DIR} / "tone.wav"};
    auto reference{wiola::codec::ffmpeg::FfmpegDecoder::open(path)};
    auto decoder{wiola::codec::ffmpeg::FfmpegDecoder::open(path)};
    ASSERT_TRUE(reference);
    ASSERT_TRUE(decoder);
    auto expected{decode_all(**reference)};
    ASSERT_TRUE(expected);
    std::array<float, 128> prefix{};
    ASSERT_TRUE((*decoder)->read(prefix));

    auto rejected{(*decoder)->seek((*decoder)->num_frames() + wiola::pcm::Frames{1})};
    ASSERT_FALSE(rejected);
    EXPECT_EQ((*decoder)->position_frames(), wiola::pcm::Frames{64});
    std::array<float, 128> output{};
    ASSERT_TRUE((*decoder)->read(output));
    EXPECT_TRUE(std::equal(output.begin(), output.end(), expected->begin() + 64 * 2));
}

TEST(FfmpegDecoder, AacSeekMatchesItsSequentialAudibleTimeline)
{
    const std::filesystem::path path{
        std::filesystem::path{WIOLA_TEST_DATA_DIR} / "multi-audio.m4a"};
    auto reference{wiola::codec::ffmpeg::FfmpegDecoder::open(path)};
    auto decoder{wiola::codec::ffmpeg::FfmpegDecoder::open(path)};
    ASSERT_TRUE(reference);
    ASSERT_TRUE(decoder);
    auto expected{decode_all(**reference)};
    ASSERT_TRUE(expected);

    ASSERT_TRUE((*decoder)->seek(wiola::pcm::Frames{1000}));
    std::array<float, 128> output{};
    std::size_t filled{0};
    while (filled < output.size()) {
        auto actual{(*decoder)->read(std::span{output}.subspan(filled))};
        ASSERT_TRUE(actual);
        ASSERT_GT(actual->frames, wiola::pcm::Frames{});
        filled += (*decoder)->spec().samples_per(actual->frames);
    }
    EXPECT_TRUE(std::equal(output.begin(), output.end(), expected->begin() + 1000 * 2));
}

TEST(FfmpegDecoder, FlacAndMpegSeeksMatchSequentialDecoding)
{
    for (const char* name : {"tone.flac", "tone.mp2", "tone.mp3"}) {
        const std::filesystem::path path{std::filesystem::path{WIOLA_TEST_DATA_DIR} / name};
        auto reference{wiola::codec::ffmpeg::FfmpegDecoder::open(path)};
        auto decoder{wiola::codec::ffmpeg::FfmpegDecoder::open(path)};
        ASSERT_TRUE(reference) << name;
        ASSERT_TRUE(decoder) << name;
        auto expected{decode_all(**reference)};
        ASSERT_TRUE(expected) << name;
        ASSERT_TRUE((*decoder)->seek(wiola::pcm::Frames{1000})) << name;
        std::array<float, 128> output{};
        std::size_t filled{0};
        while (filled < output.size()) {
            auto actual{(*decoder)->read(std::span{output}.subspan(filled))};
            ASSERT_TRUE(actual) << name;
            ASSERT_GT(actual->frames, wiola::pcm::Frames{}) << name;
            filled += (*decoder)->spec().samples_per(actual->frames);
        }
        for (std::size_t index{0}; index < output.size(); ++index)
            EXPECT_NEAR(output[index], (*expected)[2000 + index], 0.0001F) << name;
    }
}

int inspection_calls{0};
int inspection_failure{0};
int constant_receive_result{0};
int scripted_receive_calls{0};
int demux_seek_calls{0};

enum class ReceiveScript {
    error,
    end,
    missing_timestamp,
    beyond_target,
};

ReceiveScript receive_script{ReceiveScript::error};

int failing_inspection(AVFormatContext* format, AVDictionary** options)
{
    ++inspection_calls;

    if (inspection_calls == inspection_failure)
        return AVERROR_INVALIDDATA;

    return avformat_find_stream_info(format, options);
}

int overflowing_origin_inspection(AVFormatContext* format, AVDictionary** options)
{
    const int result{avformat_find_stream_info(format, options)};

    for (unsigned int index{0}; index < format->nb_streams; ++index)
        format->streams[index]->start_time = std::numeric_limits<std::int64_t>::max();

    return result;
}

int constant_receive(AVCodecContext*, AVFrame*)
{
    return constant_receive_result;
}

int scripted_receive(AVCodecContext*, AVFrame* frame)
{
    ++scripted_receive_calls;

    if (scripted_receive_calls == 1) {
        frame->nb_samples = 1;
        frame->best_effort_timestamp = 0;

        return 0;
    }

    frame->nb_samples = 1;

    switch (receive_script) {
    case ReceiveScript::error:
        return AVERROR_INVALIDDATA;
    case ReceiveScript::end:
        return AVERROR_EOF;
    case ReceiveScript::missing_timestamp:
        frame->best_effort_timestamp = AV_NOPTS_VALUE;

        return 0;
    case ReceiveScript::beyond_target:
        frame->best_effort_timestamp = 2000;

        return 0;
    }

    return AVERROR_INVALIDDATA;
}

int failing_demux_seek(AVFormatContext*, int, std::int64_t, int)
{
    return AVERROR_INVALIDDATA;
}

int failing_restart_demux_seek(AVFormatContext*, int, std::int64_t, int)
{
    ++demux_seek_calls;

    return demux_seek_calls == 1 ? 0 : AVERROR_INVALIDDATA;
}

TEST(FfmpegDecoder, ReportsLocalFileAndCodecInitializationFailures)
{
    auto missing{wiola::codec::ffmpeg::FfmpegDecoder::open("/nonexistent/wiola.wav")};

    ASSERT_FALSE(missing);
    EXPECT_EQ(missing.error().category, wiola::media::ErrorCategory::unreadable);

    wiola::codec::ffmpeg::FfmpegDecoderOptions options;
    options.packets.api.open = [](AVCodecContext*, const AVCodec*, AVDictionary**) {
        return -1;
    };

    auto invalid{wiola::codec::ffmpeg::FfmpegDecoder::open(
        std::filesystem::path{WIOLA_TEST_DATA_DIR} / "tone.wav", options
    )};

    ASSERT_FALSE(invalid);
    EXPECT_EQ(invalid.error().category, wiola::media::ErrorCategory::damaged);
}

TEST(FfmpegDecoder, EnforcesItsConversionWorkBudget)
{
    wiola::codec::ffmpeg::FfmpegDecoderOptions options;
    options.decode_work_budget = 0;
    auto decoder{wiola::codec::ffmpeg::FfmpegDecoder::open(
        std::filesystem::path{WIOLA_TEST_DATA_DIR} / "tone.wav", options
    )};
    ASSERT_TRUE(decoder);
    std::array<float, 2> output{};

    auto read{(*decoder)->read(output)};

    ASSERT_FALSE(read);
    EXPECT_EQ(read.error().category, wiola::media::ErrorCategory::limit_exceeded);
}

TEST(FfmpegDecoder, PropagatesConverterDrainFailure)
{
    wiola::codec::ffmpeg::FfmpegDecoderOptions options;
    options.conversion.convert = [](SwrContext* converter, std::uint8_t** output, int output_frames,
                                     const std::uint8_t* const * input, int input_frames) {
        if (!input)
            return -1;

        return swr_convert(converter, output, output_frames, input, input_frames);
    };
    auto decoder{wiola::codec::ffmpeg::FfmpegDecoder::open(
        std::filesystem::path{WIOLA_TEST_DATA_DIR} / "tone-vorbis.ogg", options
    )};
    ASSERT_TRUE(decoder);
    std::array<float, 128> output{};
    auto read{(*decoder)->read(output)};

    for (int calls{0}; read && !read->end_of_stream && calls < 1000; ++calls)
        read = (*decoder)->read(output);

    ASSERT_FALSE(read);
    EXPECT_EQ(read.error().category, wiola::media::ErrorCategory::damaged);
}

TEST(FfmpegDecoder, PropagatesCandidateAndOriginProbeOpenFailures)
{
    for (const int failure : {2, 3}) {
        inspection_calls = 0;
        inspection_failure = failure;
        wiola::codec::ffmpeg::FfmpegDecoderOptions options;
        options.inspect = failing_inspection;
        auto decoder{wiola::codec::ffmpeg::FfmpegDecoder::open(
            std::filesystem::path{WIOLA_TEST_DATA_DIR} / "tone.wav", options
        )};
        ASSERT_TRUE(decoder) << failure;

        auto sought{(*decoder)->seek(wiola::pcm::Frames{1})};

        ASSERT_FALSE(sought) << failure;
        EXPECT_EQ(sought.error().category, wiola::media::ErrorCategory::damaged) << failure;
    }
}

TEST(FfmpegDecoder, RejectsErrorAndEndFromTheOriginFrameProbe)
{
    for (const int result : {AVERROR_INVALIDDATA, AVERROR_EOF}) {
        constant_receive_result = result;
        wiola::codec::ffmpeg::FfmpegDecoderOptions options;
        options.packets.api.receive = constant_receive;
        auto decoder{wiola::codec::ffmpeg::FfmpegDecoder::open(
            std::filesystem::path{WIOLA_TEST_DATA_DIR} / "tone.wav", options
        )};
        ASSERT_TRUE(decoder);

        auto sought{(*decoder)->seek(wiola::pcm::Frames{1})};

        ASSERT_FALSE(sought);
    }
}

TEST(FfmpegDecoder, PropagatesErrorsAndEndDuringSeekPreroll)
{
    for (const ReceiveScript script : {ReceiveScript::error, ReceiveScript::end}) {
        scripted_receive_calls = 0;
        receive_script = script;
        wiola::codec::ffmpeg::FfmpegDecoderOptions options;
        options.packets.api.receive = scripted_receive;
        auto decoder{wiola::codec::ffmpeg::FfmpegDecoder::open(
            std::filesystem::path{WIOLA_TEST_DATA_DIR} / "tone.wav", options
        )};
        ASSERT_TRUE(decoder);

        auto sought{(*decoder)->seek(wiola::pcm::Frames{1})};

        ASSERT_FALSE(sought);
    }
}

TEST(FfmpegDecoder, RejectsSeekPrerollWithoutTimestamps)
{
    scripted_receive_calls = 0;
    receive_script = ReceiveScript::missing_timestamp;
    wiola::codec::ffmpeg::FfmpegDecoderOptions options;
    options.packets.api.receive = scripted_receive;
    auto decoder{wiola::codec::ffmpeg::FfmpegDecoder::open(
        std::filesystem::path{WIOLA_TEST_DATA_DIR} / "tone.wav", options
    )};
    ASSERT_TRUE(decoder);

    auto sought{(*decoder)->seek(wiola::pcm::Frames{1})};

    ASSERT_FALSE(sought);
    EXPECT_EQ(sought.error().category, wiola::media::ErrorCategory::seek_failed);
}

TEST(FfmpegDecoder, RejectsADemuxerThatCannotSeekToOrigin)
{
    wiola::codec::ffmpeg::FfmpegDecoderOptions options;
    options.seek = failing_demux_seek;
    auto decoder{wiola::codec::ffmpeg::FfmpegDecoder::open(
        std::filesystem::path{WIOLA_TEST_DATA_DIR} / "tone.wav", options
    )};
    ASSERT_TRUE(decoder);

    auto sought{(*decoder)->seek(wiola::pcm::Frames{1})};

    ASSERT_FALSE(sought);
    EXPECT_EQ(sought.error().category, wiola::media::ErrorCategory::seek_failed);
}

TEST(FfmpegDecoder, RejectsADemuxerThatCannotRestartSeekPreroll)
{
    scripted_receive_calls = 0;
    receive_script = ReceiveScript::beyond_target;
    demux_seek_calls = 0;
    wiola::codec::ffmpeg::FfmpegDecoderOptions options;
    options.packets.api.receive = scripted_receive;
    options.seek = failing_restart_demux_seek;
    auto decoder{wiola::codec::ffmpeg::FfmpegDecoder::open(
        std::filesystem::path{WIOLA_TEST_DATA_DIR} / "tone.wav", options
    )};
    ASSERT_TRUE(decoder);

    auto sought{(*decoder)->seek(wiola::pcm::Frames{1})};

    ASSERT_FALSE(sought);
    EXPECT_EQ(sought.error().category, wiola::media::ErrorCategory::seek_failed);
}

TEST(FfmpegDecoder, PropagatesConversionFailureDuringSeekPreroll)
{
    wiola::codec::ffmpeg::FfmpegDecoderOptions options;
    options.conversion.out_samples = [](SwrContext*, int) {
        return -1;
    };
    auto decoder{wiola::codec::ffmpeg::FfmpegDecoder::open(
        std::filesystem::path{WIOLA_TEST_DATA_DIR} / "tone.wav", options
    )};
    ASSERT_TRUE(decoder);

    auto sought{(*decoder)->seek(wiola::pcm::Frames{1})};

    ASSERT_FALSE(sought);
    EXPECT_EQ(sought.error().category, wiola::media::ErrorCategory::limit_exceeded);
}

TEST(FfmpegDecoder, EnforcesItsSeekPrerollBudget)
{
    wiola::codec::ffmpeg::FfmpegDecoderOptions options;
    options.seek_work_budget = 0;
    auto decoder{wiola::codec::ffmpeg::FfmpegDecoder::open(
        std::filesystem::path{WIOLA_TEST_DATA_DIR} / "tone.wav", options
    )};
    ASSERT_TRUE(decoder);

    auto sought{(*decoder)->seek(wiola::pcm::Frames{1})};

    ASSERT_FALSE(sought);
    EXPECT_EQ(sought.error().category, wiola::media::ErrorCategory::limit_exceeded);
}

TEST(FfmpegDecoder, SeeksDirectlyToTheExactEnd)
{
    auto decoder{wiola::codec::ffmpeg::FfmpegDecoder::open(
        std::filesystem::path{WIOLA_TEST_DATA_DIR} / "tone.wav"
    )};
    ASSERT_TRUE(decoder);

    auto sought{(*decoder)->seek((*decoder)->num_frames())};

    ASSERT_TRUE(sought);
    EXPECT_EQ(sought->achieved, (*decoder)->num_frames());
    EXPECT_TRUE((*decoder)->exhausted());
}

TEST(FfmpegDecoder, RejectsSeekFramesOutsideFfmpegTimestamps)
{
    auto decoder{wiola::codec::ffmpeg::FfmpegDecoder::open(
        std::filesystem::path{WIOLA_TEST_DATA_DIR} / "tone-vorbis.ogg"
    )};
    ASSERT_TRUE(decoder);

    auto sought{(*decoder)->seek(wiola::pcm::Frames{std::numeric_limits<std::size_t>::max()})};

    ASSERT_FALSE(sought);
    EXPECT_EQ(sought.error().category, wiola::media::ErrorCategory::seek_failed);
}

TEST(FfmpegDecoder, RejectsAnOriginThatOverflowsTheSeekTimestamp)
{
    wiola::codec::ffmpeg::FfmpegDecoderOptions options;
    options.inspect = overflowing_origin_inspection;
    auto decoder{wiola::codec::ffmpeg::FfmpegDecoder::open(
        std::filesystem::path{WIOLA_TEST_DATA_DIR} / "tone.wav", options
    )};
    ASSERT_TRUE(decoder);

    auto sought{(*decoder)->seek(wiola::pcm::Frames{1})};

    ASSERT_FALSE(sought);
    EXPECT_EQ(sought.error().category, wiola::media::ErrorCategory::seek_failed);
}

} // namespace
