/**
 * @file
 * @brief Provider-level AAC-LC and ALAC qualification in MP4/M4A.
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

#include <codec/provider.hpp>

#include <gtest/gtest.h>

#include <algorithm>
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
                            .detail = "MP4 decode did not finish"}
    };
}

wiola::codec::Opened open_mp4(const char* name)
{
    wiola::codec::MediaBackendRegistry registry;
    return registry.open(std::filesystem::path{WIOLA_TEST_DATA_DIR} / name);
}

TEST(Mp4Qualification, AacLcAndAlacDecodeTheirCompleteAudibleTimelines)
{
    wiola::codec::LocalMediaProvider provider;
    for (const char* name : {"multi-audio.m4a", "attached-art.m4a", "tone-alac.mp4"}) {
        auto asset{provider.open(std::filesystem::path{WIOLA_TEST_DATA_DIR} / name)};
        ASSERT_TRUE(asset) << name;
        auto decoder{(*asset)->create_decoder()};
        ASSERT_TRUE(decoder) << name;
        EXPECT_EQ((*decoder)->spec().sample_rate, wiola::units::Frequency{44100}) << name;
        EXPECT_EQ((*decoder)->spec().num_channels, 2U) << name;
        EXPECT_EQ((*decoder)->length_accuracy(), wiola::codec::LengthAccuracy::exact) << name;
        EXPECT_EQ((*decoder)->num_frames(), wiola::pcm::Frames{11025}) << name;
        auto samples{read_all(**decoder)};
        ASSERT_TRUE(samples) << name;
        EXPECT_EQ(samples->size(), 11025U * 2U) << name;
    }
}

TEST(Mp4Qualification, AacLcAndAlacSeeksMatchSequentialSamples)
{
    for (const char* name : {"multi-audio.m4a", "tone-alac.mp4"}) {
        auto reference{open_mp4(name)};
        auto sought{open_mp4(name)};
        ASSERT_TRUE(reference) << name;
        ASSERT_TRUE(sought) << name;
        auto expected{read_all(**reference)};
        ASSERT_TRUE(expected) << name;
        auto outcome{(*sought)->seek(wiola::pcm::Frames{1000})};
        ASSERT_TRUE(outcome) << name;
        EXPECT_EQ(outcome->achieved, wiola::pcm::Frames{1000}) << name;
        EXPECT_TRUE(outcome->exact) << name;

        std::array<float, 128> output{};
        std::size_t filled{0};
        while (filled < output.size()) {
            auto read{(*sought)->read(std::span{output}.subspan(filled))};
            ASSERT_TRUE(read) << name;
            ASSERT_GT(read->frames, wiola::pcm::Frames{}) << name;
            filled += (*sought)->spec().samples_per(read->frames);
        }
        EXPECT_TRUE(std::equal(output.begin(), output.end(), expected->begin() + 2000)) << name;
    }
}

TEST(Mp4Qualification, AttachedArtworkAndUnicodeMetadataRemainAvailable)
{
    wiola::codec::LocalMediaProvider provider;
    auto asset{provider.open(std::filesystem::path{WIOLA_TEST_DATA_DIR} / "attached-art.m4a")};
    ASSERT_TRUE(asset);

    auto metadata{(*asset)->read_metadata()};
    ASSERT_TRUE(metadata);
    EXPECT_EQ(metadata->track.title, "Тестовый трек");
    EXPECT_EQ(metadata->track.artist, "Артист");
    EXPECT_EQ(metadata->artwork.mime_type, "image/png");
    EXPECT_FALSE(metadata->artwork.empty());
}

TEST(Mp4Qualification, RejectsAnMp4WithoutAudio)
{
    auto decoder{open_mp4("no-audio.mp4")};

    ASSERT_FALSE(decoder);
    EXPECT_EQ(decoder.error().category, wiola::media::ErrorCategory::unsupported);
}

} // namespace
