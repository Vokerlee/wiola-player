/**
 * @file
 * @brief WavPack, Monkey's Audio, and TTA qualification.
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

#include <media/local/provider.hpp>

#include <media/local/resource.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <string_view>
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
                            .detail = "lossless-family decode did not finish"}
    };
}

wiola::media::DecoderOpenResult open_file(std::string_view name)
{
    wiola::codec::MediaBackendRegistry registry;
    return registry.open(std::filesystem::path{WIOLA_TEST_DATA_DIR} / name);
}

void expect_complete_timeline(wiola::media::AudioDecoder& decoder)
{
    EXPECT_EQ(decoder.spec().sample_rate, wiola::units::Frequency{44100});
    EXPECT_EQ(decoder.spec().num_channels, 2U);
    EXPECT_EQ(decoder.length_accuracy(), wiola::media::LengthAccuracy::exact);
    EXPECT_EQ(decoder.num_frames(), wiola::pcm::Frames{11025});
}

TEST(LosslessFamilyQualification, LosslessVariantsMatchTheReferencePcmExactly)
{
    auto reference{open_file("tone.wav")};
    ASSERT_TRUE(reference);
    auto expected{read_all(**reference)};
    ASSERT_TRUE(expected);

    wiola::codec::LocalMediaProvider provider;
    for (const std::string_view name : {"tone-lossless.wv", "tone.ape", "tone.tta"}) {
        auto asset{provider.open(
            wiola::media::local_resource(std::filesystem::path{WIOLA_TEST_DATA_DIR} / name)
        )};
        ASSERT_TRUE(asset) << name;
        auto decoder{(*asset)->create_decoder()};
        ASSERT_TRUE(decoder) << name;
        expect_complete_timeline(**decoder);
        auto samples{read_all(**decoder)};
        ASSERT_TRUE(samples) << name;
        EXPECT_EQ(*samples, *expected) << name;
    }
}

TEST(LosslessFamilyQualification, ReadsUnicodeApeMetadata)
{
    wiola::codec::LocalMediaProvider provider;
    auto asset{provider.open(
        wiola::media::local_resource(std::filesystem::path{WIOLA_TEST_DATA_DIR} / "tone.ape")
    )};
    ASSERT_TRUE(asset);

    auto metadata{(*asset)->read_metadata()};
    ASSERT_TRUE(metadata);
    EXPECT_EQ(metadata->track.artist, "Исполнитель");
    EXPECT_EQ(metadata->track.title, "Обезьяний тест");
}

TEST(LosslessFamilyQualification, HybridWavPackHasExactTimingButNotLosslessPcm)
{
    auto reference{open_file("tone.wav")};
    auto hybrid{open_file("tone-hybrid.wv")};
    ASSERT_TRUE(reference);
    ASSERT_TRUE(hybrid);
    expect_complete_timeline(**hybrid);
    auto expected{read_all(**reference)};
    auto samples{read_all(**hybrid)};
    ASSERT_TRUE(expected);
    ASSERT_TRUE(samples);
    ASSERT_EQ(samples->size(), expected->size());
    EXPECT_FALSE(std::equal(samples->begin(), samples->end(), expected->begin()));
}

TEST(LosslessFamilyQualification, EveryVariantSeeksToItsSequentialTimeline)
{
    for (const std::string_view name : {
             "tone-lossless.wv",
             "tone-hybrid.wv",
             "tone.ape",
             "tone.tta",
         }) {
        auto reference{open_file(name)};
        auto sought{open_file(name)};
        ASSERT_TRUE(reference) << name;
        ASSERT_TRUE(sought) << name;
        auto expected{read_all(**reference)};
        ASSERT_TRUE(expected) << name;
        auto outcome{(*sought)->seek(wiola::pcm::Frames{4000})};
        ASSERT_TRUE(outcome) << name;
        EXPECT_EQ(outcome->achieved, wiola::pcm::Frames{4000}) << name;
        EXPECT_TRUE(outcome->exact) << name;

        std::array<float, 128> output{};
        std::size_t filled{0};
        while (filled < output.size()) {
            auto read{(*sought)->read(std::span{output}.subspan(filled))};
            ASSERT_TRUE(read) << name;
            ASSERT_GT(read->frames, wiola::pcm::Frames{}) << name;
            filled += (*sought)->spec().samples_per(read->frames);
        }
        EXPECT_TRUE(std::equal(output.begin(), output.end(), expected->begin() + 8000)) << name;
    }
}

} // namespace
