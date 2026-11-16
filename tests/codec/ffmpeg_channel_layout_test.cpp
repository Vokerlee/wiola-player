/**
 * @file
 * @brief Semantic multichannel layout and channel-order qualification.
 * @author Roman Glaz
 * @copyright © 2026, <vokerlee@gmail.com>
 */

#include <codec/provider.hpp>

#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <vector>

namespace {

wiola::media::Result<std::vector<float>> read_all(wiola::codec::Decoder& decoder)
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
    wiola::codec::LocalMediaProvider provider;
    auto asset{provider.open(std::filesystem::path{WIOLA_TEST_DATA_DIR} / "channels-5.1.caf")};
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
    wiola::codec::LocalMediaProvider provider;
    auto asset{provider.open(std::filesystem::path{WIOLA_TEST_DATA_DIR} / "channels-6.1.caf")};

    ASSERT_TRUE(asset);
    auto decoder{(*asset)->create_decoder()};
    ASSERT_FALSE(decoder);
    EXPECT_EQ(decoder.error().category, wiola::media::ErrorCategory::unsupported);
}

} // namespace
