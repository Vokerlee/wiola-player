/**
 * @file
 * @brief FFmpeg packet/frame progress and draining tests.
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

#include <media/codecs/ffmpeg/packet_decoder.hpp>
#include <wiola/media/local/file_stream.hpp>

#include <gtest/gtest.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/mem.h>
}

#include <cstddef>
#include <filesystem>
#include <limits>
#include <memory>
#include <stdexcept>
#include <stop_token>

namespace {

wiola::media::Result<wiola::pcm::Frames> decode_fixture(const char* name)
{
    auto stream{
        wiola::media::LocalFileStream::open(std::filesystem::path{WIOLA_TEST_DATA_DIR} / name)};

    if (!stream)
        return wiola::media::unexpected{stream.error()};

    auto input{wiola::codec::ffmpeg::FfmpegInput::open(std::move(*stream))};

    if (!input)
        return wiola::media::unexpected{input.error()};

    auto facts{(*input)->inspect()};

    if (!facts)
        return wiola::media::unexpected{facts.error()};

    auto decoder{wiola::codec::ffmpeg::PacketDecoder::open(**input, *facts)};

    if (!decoder)
        return wiola::media::unexpected{decoder.error()};

    wiola::pcm::Frames total{};

    for (int calls{0}; calls < 10000; ++calls) {
        auto event{(*decoder)->next_frame()};

        if (!event)
            return wiola::media::unexpected{event.error()};

        if (event->end_of_stream) {
            auto again{(*decoder)->next_frame()};

            if (!again)
                return wiola::media::unexpected{again.error()};

            EXPECT_TRUE(again->end_of_stream);
            EXPECT_EQ(again->frame, nullptr);

            return total;
        }

        EXPECT_NE(event->frame, nullptr);
        EXPECT_GT(event->frame->nb_samples, 0);

        total += wiola::pcm::Frames{static_cast<std::size_t>(event->frame->nb_samples)};
    }

    return wiola::media::unexpected{
        wiola::media::Error{.category = wiola::media::ErrorCategory::limit_exceeded,
                            .detail = "test did not finish"}
    };
}

TEST(FfmpegPacketDecoder, DecodesEveryPcmFrameAndDrains)
{
    auto decoded{decode_fixture("tone.wav")};

    ASSERT_TRUE(decoded);
    EXPECT_EQ(*decoded, wiola::pcm::Frames{11025});
}

TEST(FfmpegPacketDecoder, DecodesDelayedAacAndReachesRealEof)
{
    auto decoded{decode_fixture("multi-audio.m4a")};

    ASSERT_TRUE(decoded);
    EXPECT_GT(*decoded, wiola::pcm::Frames{});
}

struct InputFixture {
    std::unique_ptr<wiola::codec::ffmpeg::FfmpegInput> input;
    wiola::codec::ffmpeg::FfmpegStreamFacts facts;
};

InputFixture open_input(const char* name, const wiola::media::OperationContext& context = {})
{
    auto stream{
        wiola::media::LocalFileStream::open(std::filesystem::path{WIOLA_TEST_DATA_DIR} / name)};

    if (!stream)
        throw std::runtime_error{"fixture stream did not open"};

    auto input{wiola::codec::ffmpeg::FfmpegInput::open(std::move(*stream), context)};

    if (!input)
        throw std::runtime_error{"fixture input did not open"};

    auto facts{(*input)->inspect()};

    if (!facts)
        throw std::runtime_error{"fixture input was not inspected"};

    return {std::move(*input), *facts};
}

class AllocationLimit final {
public:
    explicit AllocationLimit(std::size_t bytes) { av_max_alloc(bytes); }

    ~AllocationLimit() { av_max_alloc(std::numeric_limits<std::size_t>::max()); }
};

TEST(FfmpegPacketDecoder, RejectsAnUnavailableCodec)
{
    auto fixture{open_input("tone.wav")};
    fixture.input->format()->streams[fixture.facts.stream_index]->codecpar->codec_id =
        AV_CODEC_ID_NONE;

    auto decoder{wiola::codec::ffmpeg::PacketDecoder::open(*fixture.input, fixture.facts)};

    ASSERT_FALSE(decoder);
    EXPECT_EQ(decoder.error().category, wiola::media::ErrorCategory::unsupported);
}

TEST(FfmpegPacketDecoder, ReportsCodecAllocationFailure)
{
    auto fixture{open_input("tone.wav")};
    AllocationLimit limited{1};

    auto decoder{wiola::codec::ffmpeg::PacketDecoder::open(*fixture.input, fixture.facts)};

    ASSERT_FALSE(decoder);
    EXPECT_EQ(decoder.error().category, wiola::media::ErrorCategory::limit_exceeded);
}

TEST(FfmpegPacketDecoder, RejectsInvalidCodecParameters)
{
    auto fixture{open_input("tone.wav")};
    wiola::codec::ffmpeg::PacketDecoderOptions options;
    options.api.parameters = [](AVCodecContext*, const AVCodecParameters*) {
        return -1;
    };

    auto decoder{wiola::codec::ffmpeg::PacketDecoder::open(*fixture.input, fixture.facts, options)};

    ASSERT_FALSE(decoder);
    EXPECT_EQ(decoder.error().category, wiola::media::ErrorCategory::damaged);
}

TEST(FfmpegPacketDecoder, ReportsCodecOpenFailure)
{
    auto fixture{open_input("tone.wav")};
    wiola::codec::ffmpeg::PacketDecoderOptions options;
    options.api.open = [](AVCodecContext*, const AVCodec*, AVDictionary**) {
        return -1;
    };

    auto decoder{wiola::codec::ffmpeg::PacketDecoder::open(*fixture.input, fixture.facts, options)};

    ASSERT_FALSE(decoder);
    EXPECT_EQ(decoder.error().category, wiola::media::ErrorCategory::damaged);
}

TEST(FfmpegPacketDecoder, ReportsFrameReceptionFailure)
{
    auto fixture{open_input("tone.wav")};
    wiola::codec::ffmpeg::PacketDecoderOptions options;
    options.api.receive = [](AVCodecContext*, AVFrame*) {
        return AVERROR_INVALIDDATA;
    };

    auto decoder{wiola::codec::ffmpeg::PacketDecoder::open(*fixture.input, fixture.facts, options)};

    ASSERT_TRUE(decoder);

    auto event{(*decoder)->next_frame()};

    ASSERT_FALSE(event);
    EXPECT_EQ(event.error().category, wiola::media::ErrorCategory::damaged);
}

TEST(FfmpegPacketDecoder, RejectsAStalledPacketPump)
{
    auto fixture{open_input("tone.wav")};
    wiola::codec::ffmpeg::PacketDecoderOptions options;
    options.api.receive = [](AVCodecContext*, AVFrame*) {
        return AVERROR(EAGAIN);
    };
    options.api.read = [](AVFormatContext*, AVPacket* packet) {
        packet->stream_index = 0;

        return 0;
    };
    options.api.send = [](AVCodecContext*, const AVPacket*) {
        return AVERROR(EAGAIN);
    };

    auto decoder{wiola::codec::ffmpeg::PacketDecoder::open(*fixture.input, fixture.facts, options)};

    ASSERT_TRUE(decoder);

    auto event{(*decoder)->next_frame()};

    ASSERT_FALSE(event);
    EXPECT_EQ(event.error().category, wiola::media::ErrorCategory::damaged);
}

TEST(FfmpegPacketDecoder, ReportsPacketSubmissionFailure)
{
    auto fixture{open_input("tone.wav")};
    wiola::codec::ffmpeg::PacketDecoderOptions options;
    options.api.receive = [](AVCodecContext*, AVFrame*) {
        return AVERROR(EAGAIN);
    };
    options.api.read = [](AVFormatContext*, AVPacket* packet) {
        packet->stream_index = 0;

        return 0;
    };
    options.api.send = [](AVCodecContext*, const AVPacket*) {
        return AVERROR_INVALIDDATA;
    };

    auto decoder{wiola::codec::ffmpeg::PacketDecoder::open(*fixture.input, fixture.facts, options)};

    ASSERT_TRUE(decoder);

    auto event{(*decoder)->next_frame()};

    ASSERT_FALSE(event);
    EXPECT_EQ(event.error().category, wiola::media::ErrorCategory::damaged);
}

TEST(FfmpegPacketDecoder, ReportsPacketDemuxingFailure)
{
    auto fixture{open_input("tone.wav")};
    wiola::codec::ffmpeg::PacketDecoderOptions options;
    options.api.receive = [](AVCodecContext*, AVFrame*) {
        return AVERROR(EAGAIN);
    };
    options.api.read = [](AVFormatContext*, AVPacket*) {
        return AVERROR_INVALIDDATA;
    };

    auto decoder{wiola::codec::ffmpeg::PacketDecoder::open(*fixture.input, fixture.facts, options)};

    ASSERT_TRUE(decoder);

    auto event{(*decoder)->next_frame()};

    ASSERT_FALSE(event);
    EXPECT_EQ(event.error().category, wiola::media::ErrorCategory::damaged);
}

TEST(FfmpegPacketDecoder, ReportsDecoderDrainFailure)
{
    auto fixture{open_input("tone.wav")};
    wiola::codec::ffmpeg::PacketDecoderOptions options;
    options.api.receive = [](AVCodecContext*, AVFrame*) {
        return AVERROR(EAGAIN);
    };
    options.api.read = [](AVFormatContext*, AVPacket*) {
        return AVERROR_EOF;
    };
    options.api.send = [](AVCodecContext*, const AVPacket*) {
        return AVERROR_INVALIDDATA;
    };

    auto decoder{wiola::codec::ffmpeg::PacketDecoder::open(*fixture.input, fixture.facts, options)};

    ASSERT_TRUE(decoder);

    auto event{(*decoder)->next_frame()};

    ASSERT_FALSE(event);
    EXPECT_EQ(event.error().category, wiola::media::ErrorCategory::damaged);
}

TEST(FfmpegPacketDecoder, RetriesWhenDecoderDrainBackpressures)
{
    auto fixture{open_input("tone.wav")};
    wiola::codec::ffmpeg::PacketDecoderOptions options;
    options.work_budget = 1;
    options.api.receive = [](AVCodecContext*, AVFrame*) {
        return AVERROR(EAGAIN);
    };
    options.api.read = [](AVFormatContext*, AVPacket*) {
        return AVERROR_EOF;
    };
    options.api.send = [](AVCodecContext*, const AVPacket*) {
        return AVERROR(EAGAIN);
    };

    auto decoder{wiola::codec::ffmpeg::PacketDecoder::open(*fixture.input, fixture.facts, options)};

    ASSERT_TRUE(decoder);

    auto event{(*decoder)->next_frame()};

    ASSERT_FALSE(event);
    EXPECT_EQ(event.error().category, wiola::media::ErrorCategory::limit_exceeded);
}

TEST(FfmpegPacketDecoder, EnforcesItsPacketWorkBudget)
{
    auto fixture{open_input("tone.wav")};
    wiola::codec::ffmpeg::PacketDecoderOptions options;
    options.work_budget = 0;

    auto decoder{wiola::codec::ffmpeg::PacketDecoder::open(*fixture.input, fixture.facts, options)};

    ASSERT_TRUE(decoder);
    EXPECT_NE((*decoder)->codec(), nullptr);

    auto event{(*decoder)->next_frame()};

    ASSERT_FALSE(event);
    EXPECT_EQ(event.error().category, wiola::media::ErrorCategory::limit_exceeded);
}

} // namespace

TEST(FfmpegPacketDecoder, CancellationPrecedesBufferedFrameDelivery)
{
    std::stop_source cancellation;
    auto fixture = open_input("tone.wav", {.stop = cancellation.get_token()});
    auto decoder = wiola::codec::ffmpeg::PacketDecoder::open(*fixture.input, fixture.facts);

    ASSERT_TRUE(decoder);

    cancellation.request_stop();

    const auto reopened = wiola::codec::ffmpeg::PacketDecoder::open(*fixture.input, fixture.facts);

    ASSERT_FALSE(reopened);
    EXPECT_EQ(reopened.error().category, wiola::media::ErrorCategory::cancelled);

    const auto event = (*decoder)->next_frame();

    ASSERT_FALSE(event);
    EXPECT_EQ(event.error().category, wiola::media::ErrorCategory::cancelled);
}

TEST(FfmpegPacketDecoder, RejectsStreamFactsOutsideItsOwnedInput)
{
    auto fixture = open_input("tone.wav");
    for (const auto index : {static_cast<std::size_t>(fixture.input->format()->nb_streams),
             std::numeric_limits<std::size_t>::max()}) {
        auto facts = fixture.facts;
        facts.stream_index = index;

        const auto decoder = wiola::codec::ffmpeg::PacketDecoder::open(*fixture.input, facts);

        ASSERT_FALSE(decoder);
        EXPECT_EQ(decoder.error().category, wiola::media::ErrorCategory::damaged);
    }
}
