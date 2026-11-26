/**
 * @file
 * @brief Endian and sample-format qualification for sampled-file containers.
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

#include <wiola/media/local/resource.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <string_view>
#include <vector>

namespace {

constexpr std::array<std::string_view, 6> sampled_files{
    "tone-s16be.aiff",
    "tone-s16le.aifc",
    "tone-s32be.au",
    "tone-f64le.caf",
    "tone-s24le.w64",
    "tone-f32le.rf64",
};

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
                            .detail = "sampled-file decode did not finish"}
    };
}

wiola::media::DecoderOpenResult open_file(std::string_view name)
{
    wiola::codec::MediaBackendRegistry registry{wiola::codec::ffmpeg::make_registry()};
    return registry.open(std::filesystem::path{WIOLA_TEST_DATA_DIR} / name);
}

TEST(SampledFileQualification, LosslessVariantsMatchTheReferencePcmExactly)
{
    auto reference{open_file("tone.wav")};
    ASSERT_TRUE(reference);
    auto expected{read_all(**reference)};
    ASSERT_TRUE(expected);
    ASSERT_EQ(expected->size(), 11025U * 2U);

    auto provider{wiola::codec::ffmpeg::make_provider()};
    for (const std::string_view name : sampled_files) {
        auto asset{provider->open(
            wiola::media::local_resource(std::filesystem::path{WIOLA_TEST_DATA_DIR} / name)
        )};
        ASSERT_TRUE(asset) << name;
        auto decoder{(*asset)->create_decoder()};
        ASSERT_TRUE(decoder) << name;
        EXPECT_EQ((*decoder)->spec().sample_rate, wiola::units::Frequency{44100}) << name;
        EXPECT_EQ((*decoder)->spec().num_channels, 2U) << name;
        EXPECT_EQ((*decoder)->length_accuracy(), wiola::media::LengthAccuracy::exact) << name;
        EXPECT_EQ((*decoder)->num_frames(), wiola::pcm::Frames{11025}) << name;
        auto samples{read_all(**decoder)};
        ASSERT_TRUE(samples) << name;
        EXPECT_EQ(*samples, *expected) << name;
    }
}

TEST(SampledFileQualification, EveryVariantSeeksToTheReferencePcmPosition)
{
    auto reference{open_file("tone.wav")};
    ASSERT_TRUE(reference);
    auto expected{read_all(**reference)};
    ASSERT_TRUE(expected);

    for (const std::string_view name : sampled_files) {
        auto decoder{open_file(name)};
        ASSERT_TRUE(decoder) << name;
        auto outcome{(*decoder)->seek(wiola::pcm::Frames{4000})};
        ASSERT_TRUE(outcome) << name;
        EXPECT_EQ(outcome->achieved, wiola::pcm::Frames{4000}) << name;
        EXPECT_TRUE(outcome->exact) << name;

        std::array<float, 128> output{};
        std::size_t filled{0};
        while (filled < output.size()) {
            auto read{(*decoder)->read(std::span{output}.subspan(filled))};
            ASSERT_TRUE(read) << name;
            ASSERT_GT(read->frames, wiola::pcm::Frames{}) << name;
            filled += (*decoder)->spec().samples_per(read->frames);
        }
        EXPECT_TRUE(std::equal(output.begin(), output.end(), expected->begin() + 8000)) << name;
    }
}

} // namespace
