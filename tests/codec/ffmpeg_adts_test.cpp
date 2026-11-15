/**
 * @file
 * @brief Provider-level ADTS AAC-LC qualification.
 * @author Roman Glaz
 * @copyright © 2026, <vokerlee@gmail.com>
 */

#include <codec/provider.hpp>

#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <tuple>
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
                            .detail = "ADTS decode did not finish"}
    };
}

wiola::codec::Opened open_adts(const char* name)
{
    wiola::codec::MediaBackendRegistry registry;
    return registry.open(std::filesystem::path{WIOLA_TEST_DATA_DIR} / name);
}

TEST(AdtsQualification, AacLcUsesActualRateAndChannelsWithEstimatedLength)
{
    wiola::codec::LocalMediaProvider provider;
    for (const auto& [name, rate, channels, frames] : {
             std::tuple{"tone-lc.aac",      44100, 2U, 12288U},
             std::tuple{"tone-lc-mono.aac", 22050, 1U, 7168U },
    }) {
        auto asset{provider.open(std::filesystem::path{WIOLA_TEST_DATA_DIR} / name)};
        ASSERT_TRUE(asset) << name;
        auto decoder{(*asset)->create_decoder()};
        ASSERT_TRUE(decoder) << name;
        EXPECT_EQ((*decoder)->spec().sample_rate, wiola::units::Frequency{rate}) << name;
        EXPECT_EQ((*decoder)->spec().num_channels, channels) << name;
        EXPECT_EQ((*decoder)->length_accuracy(), wiola::codec::LengthAccuracy::estimated) << name;
        EXPECT_GT((*decoder)->num_frames(), wiola::pcm::Frames{}) << name;
        auto samples{read_all(**decoder)};
        ASSERT_TRUE(samples) << name;
        EXPECT_EQ(samples->size(), frames * channels) << name;
    }
}

TEST(AdtsQualification, AacLcSeekMatchesItsSequentialTimeline)
{
    auto reference{open_adts("tone-lc.aac")};
    auto sought{open_adts("tone-lc.aac")};
    ASSERT_TRUE(reference);
    ASSERT_TRUE(sought);
    auto expected{read_all(**reference)};
    ASSERT_TRUE(expected);
    auto outcome{(*sought)->seek(wiola::pcm::Frames{1000})};
    ASSERT_TRUE(outcome);
    EXPECT_EQ(outcome->achieved, wiola::pcm::Frames{1000});
    EXPECT_TRUE(outcome->exact);

    std::array<float, 128> output{};
    std::size_t filled{0};
    while (filled < output.size()) {
        auto read{(*sought)->read(std::span{output}.subspan(filled))};
        ASSERT_TRUE(read);
        ASSERT_GT(read->frames, wiola::pcm::Frames{});
        filled += (*sought)->spec().samples_per(read->frames);
    }
    for (std::size_t index{0}; index < output.size(); ++index)
        EXPECT_NEAR(output[index], (*expected)[2000 + index], 0.0001F) << "sample " << index;
}

} // namespace
