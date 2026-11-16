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

#include <codec/backends/ffmpeg/packet_decoder.hpp>

#include <media/local_file_stream.hpp>

#include <gtest/gtest.h>

extern "C" {
#include <libavcodec/avcodec.h>
}

#include <filesystem>
#include <memory>

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

} // namespace
