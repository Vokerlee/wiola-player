/**
 * @file
 * @brief Semantic multichannel layout and channel-order qualification.
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

#include <media/codecs/ffmpeg/composition.hpp>

#include <media/local/resource.hpp>

#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <vector>

namespace {

wiola::media::Result<std::vector<float>> read_all(wiola::media::AudioDecoder& decoder)
{
    std::vector<float> samples;
    std::array<float, 126> block{};
    for (int calls{0}; calls < 10000; ++calls) {
        auto read{decoder.read(block)};
        if (!read)
            return wiola::media::unexpected{read.error()};
        const std::size_t count{decoder.spec().samples_per(read->frames)};
        samples.insert(samples.end(), block.begin(),
            block.begin() + static_cast<std::ptrdiff_t>(count));
        if (read->end_of_stream)
            return samples;
    }
    return wiola::media::unexpected{
        wiola::media::Error{.category = wiola::media::ErrorCategory::limit_exceeded,
                            .detail = "channel-layout decode did not finish"}
    };
}

TEST(ChannelLayoutQualification, PreservesEveryFivePointOneSideChannel)
{
    auto provider{wiola::codec::ffmpeg::make_provider()};
    auto asset{
        provider->open(wiola::media::local_resource(std::filesystem::path{WIOLA_TEST_DATA_DIR} /
            "channels-5.1.caf"))};
    ASSERT_TRUE(asset);
    auto decoder{(*asset)->create_decoder()};
    ASSERT_TRUE(decoder);
    EXPECT_EQ((*decoder)->spec().channel_layout, wiola::pcm::ChannelLayout::five_point_one_side);
    EXPECT_EQ((*decoder)->spec().num_channels, 6U);
    EXPECT_EQ((*decoder)->num_frames(), wiola::pcm::Frames{11025});

    auto samples{read_all(**decoder)};
    ASSERT_TRUE(samples);
    ASSERT_EQ(samples->size(), 11025U * 6U);
    constexpr std::array expected{0.1F, 0.2F, 0.3F, 0.4F, 0.5F, 0.6F};
    for (std::size_t frame{0}; frame < 11025U; ++frame)
        for (std::size_t channel{0}; channel < expected.size(); ++channel)
            EXPECT_NEAR((*samples)[frame * 6U + channel], expected[channel], 0.0001F)
                << "frame " << frame << " channel " << channel;
}

TEST(ChannelLayoutQualification, RejectsAnUnsupportedSixPointOneMapping)
{
    auto provider{wiola::codec::ffmpeg::make_provider()};
    auto asset{
        provider->open(wiola::media::local_resource(std::filesystem::path{WIOLA_TEST_DATA_DIR} /
            "channels-6.1.caf"))};

    ASSERT_TRUE(asset);
    auto decoder{(*asset)->create_decoder()};
    ASSERT_FALSE(decoder);
    EXPECT_EQ(decoder.error().category, wiola::media::ErrorCategory::unsupported);
}

} // namespace
