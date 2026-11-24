/**
 * @file
 * @brief ASF/WMA and Matroska/WebM audio qualification.
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

#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <string_view>
#include <tuple>
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
                            .detail = "container decode did not finish"}
    };
}

wiola::media::DecoderOpenResult open_file(std::string_view name)
{
    wiola::codec::MediaBackendRegistry registry;
    return registry.open(std::filesystem::path{WIOLA_TEST_DATA_DIR} / name);
}

TEST(ContainerQualification, SelectedLegacyAndEbmlStreamsDrainWithActualPcmShapes)
{
    wiola::codec::LocalMediaProvider provider;
    for (const auto& [name, rate, frames] : {
             std::tuple{"tone-wmav2.wma",  44100, 10240U},
             std::tuple{"tone-vorbis.mka", 44100, 10897U},
             std::tuple{"tone-opus.mka",   48000, 12000U},
             std::tuple{"tone-opus.webm",  48000, 12000U},
    }) {
        auto asset{provider.open(std::filesystem::path{WIOLA_TEST_DATA_DIR} / name)};
        ASSERT_TRUE(asset) << name;
        auto decoder{(*asset)->create_decoder()};
        ASSERT_TRUE(decoder) << name;
        EXPECT_EQ((*decoder)->spec().sample_rate, wiola::units::Frequency{rate}) << name;
        EXPECT_EQ((*decoder)->spec().num_channels, 2U) << name;
        EXPECT_EQ((*decoder)->length_accuracy(), wiola::media::LengthAccuracy::estimated) << name;
        EXPECT_TRUE((*decoder)->seekable()) << name;
        auto samples{read_all(**decoder)};
        ASSERT_TRUE(samples) << name;
        EXPECT_EQ(samples->size(), frames * 2U) << name;
    }
}

TEST(ContainerQualification, SelectedStreamsSeekToTheirSequentialTimelines)
{
    for (const std::string_view name : {
             "tone-wmav2.wma",
             "tone-vorbis.mka",
             "tone-opus.mka",
             "tone-opus.webm",
         }) {
        auto reference{open_file(name)};
        auto sought{open_file(name)};
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
        for (std::size_t index{0}; index < output.size(); ++index)
            EXPECT_NEAR(output[index], (*expected)[2000 + index], 0.0001F) << name;
    }
}

TEST(ContainerQualification, ReadsUnicodeWmaMetadata)
{
    wiola::codec::LocalMediaProvider provider;
    auto asset{provider.open(std::filesystem::path{WIOLA_TEST_DATA_DIR} / "tone-wmav2.wma")};
    ASSERT_TRUE(asset);

    auto metadata{(*asset)->read_metadata()};
    ASSERT_TRUE(metadata);
    EXPECT_EQ(metadata->track.artist, "Исполнитель");
    EXPECT_EQ(metadata->track.title, "ВМА тест");
}

} // namespace
