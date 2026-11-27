/**
 * @file
 * @brief Qualifies the pinned FFmpeg build and its actual audio components.
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

#include <gtest/gtest.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}

#include <array>
#include <cerrno>
#include <filesystem>
#include <memory>
#include <string_view>

namespace {

TEST(FfmpegBuild, PinsTheAuditedReleaseAndRequiredComponents)
{
    EXPECT_STREQ(av_version_info(), "7.1.1");

    constexpr std::array codecs{
        AV_CODEC_ID_AAC,
        AV_CODEC_ID_ALAC,
        AV_CODEC_ID_APE,
        AV_CODEC_ID_FLAC,
        AV_CODEC_ID_MP2,
        AV_CODEC_ID_MP3,
        AV_CODEC_ID_MUSEPACK7,
        AV_CODEC_ID_MUSEPACK8,
        AV_CODEC_ID_OPUS,
        AV_CODEC_ID_PCM_S16BE,
        AV_CODEC_ID_PCM_S16LE,
        AV_CODEC_ID_TTA,
        AV_CODEC_ID_VORBIS,
        AV_CODEC_ID_WAVPACK,
        AV_CODEC_ID_WMAV1,
        AV_CODEC_ID_WMAV2,
    };

    for (const AVCodecID codec : codecs)
        EXPECT_NE(avcodec_find_decoder(codec), nullptr) << avcodec_get_name(codec);

    constexpr std::array<std::string_view, 17> demuxers{
        "aac",
        "aiff",
        "ape",
        "asf",
        "au",
        "caf",
        "flac",
        "matroska",
        "mov",
        "mp3",
        "mpc",
        "mpc8",
        "ogg",
        "tta",
        "w64",
        "wav",
        "wv",
    };

    for (const std::string_view demuxer : demuxers)
        EXPECT_NE(av_find_input_format(std::string{demuxer}.c_str()), nullptr) << demuxer;
}

TEST(FfmpegBuild, ProbesOnlyTheRequiredMetadataMuxers)
{
    constexpr std::array<std::string_view, 6> muxers{"flac", "ipod", "mov", "mp3", "mp4", "ogg"};

    for (const std::string_view muxer : muxers)
        EXPECT_NE(av_guess_format(std::string{muxer}.c_str(), nullptr, nullptr), nullptr) << muxer;

    EXPECT_EQ(av_guess_format("avi", nullptr, nullptr), nullptr);
}

TEST(FfmpegBuild, ExposesOnlyLocalFileInputProtocols)
{
    void* opaque{nullptr};
    bool file{false};

    while (const char* name{avio_enum_protocols(&opaque, 0)}) {
        EXPECT_FALSE(std::string_view{name} == "http" || std::string_view{name} == "https" ||
            std::string_view{name} == "tcp" || std::string_view{name} == "udp");
        file = file || std::string_view{name} == "file";
    }

    EXPECT_TRUE(file);
}

TEST(FfmpegBuild, DemuxesAndDecodesAudioThroughLibraries)
{
    AVFormatContext* input{nullptr};
    const std::filesystem::path fixture{std::filesystem::path{WIOLA_TEST_DATA_DIR} / "tone.wav"};

    ASSERT_GE(avformat_open_input(&input, fixture.string().c_str(), nullptr, nullptr), 0);

    std::unique_ptr<AVFormatContext, void (*)(AVFormatContext*)> format{input,
        [](AVFormatContext* context) { avformat_close_input(&context); }};

    ASSERT_GE(avformat_find_stream_info(format.get(), nullptr), 0);

    int stream_index{-1};

    for (unsigned int index{0}; index < format->nb_streams; ++index) {
        if (format->streams[index]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            stream_index = static_cast<int>(index);
            break;
        }
    }

    ASSERT_GE(stream_index, 0);

    const AVCodec* codec{avcodec_find_decoder(format->streams[stream_index]->codecpar->codec_id)};

    ASSERT_NE(codec, nullptr);

    std::unique_ptr<AVCodecContext, void (*)(AVCodecContext*)> decoder{
        avcodec_alloc_context3(codec),
        [](AVCodecContext* context) { avcodec_free_context(&context); }};

    ASSERT_NE(decoder, nullptr);
    ASSERT_GE(avcodec_parameters_to_context(decoder.get(), format->streams[stream_index]->codecpar),
        0);
    ASSERT_GE(avcodec_open2(decoder.get(), codec, nullptr), 0);

    std::unique_ptr<AVPacket, void (*)(AVPacket*)> packet{av_packet_alloc(),
        [](AVPacket* value) { av_packet_free(&value); }};
    std::unique_ptr<AVFrame, void (*)(AVFrame*)> frame{av_frame_alloc(),
        [](AVFrame* value) { av_frame_free(&value); }};

    ASSERT_NE(packet, nullptr);
    ASSERT_NE(frame, nullptr);

    bool decoded{false};

    while (av_read_frame(format.get(), packet.get()) >= 0) {
        if (packet->stream_index == stream_index) {
            ASSERT_GE(avcodec_send_packet(decoder.get(), packet.get()), 0);
            while (avcodec_receive_frame(decoder.get(), frame.get()) >= 0) {
                EXPECT_GT(frame->nb_samples, 0);
                EXPECT_EQ(frame->sample_rate, 44100);
                EXPECT_EQ(frame->ch_layout.nb_channels, 2);

                decoded = true;
                av_frame_unref(frame.get());
            }
        }
        av_packet_unref(packet.get());
    }

    EXPECT_TRUE(decoded);

    ASSERT_GE(avcodec_send_packet(decoder.get(), nullptr), 0);

    while (avcodec_receive_frame(decoder.get(), frame.get()) >= 0)
        av_frame_unref(frame.get());
}

} // namespace
