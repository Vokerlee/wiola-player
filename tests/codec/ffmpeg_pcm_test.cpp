/**
 * @file
 * @brief FFmpeg PCM conversion and neutral decoder adapter tests.
 * @author Roman Glaz
 * @copyright © 2026, <vokerlee@gmail.com>
 */

#include <codec/backends/ffmpeg/decoder.hpp>
#include <codec/backends/ffmpeg/pcm_converter.hpp>

#include <gtest/gtest.h>

extern "C" {
#include <libavutil/frame.h>
}

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
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

wiola::media::Result<std::vector<float>> decode_all(wiola::codec::Decoder& decoder)
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

} // namespace
