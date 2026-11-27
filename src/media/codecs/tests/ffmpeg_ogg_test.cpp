/**
 * @file
 * @brief Provider-level Ogg Vorbis and Ogg Opus qualification.
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

#include <wiola/media/codecs/ffmpeg/composition.hpp>
#include <wiola/media/local/resource.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <filesystem>
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
                            .detail = "Ogg decode did not finish"}
    };
}

wiola::media::DecoderOpenResult open_ogg(const char* name)
{
    wiola::codec::MediaBackendRegistry registry{wiola::codec::ffmpeg::make_registry()};
    return registry.open(std::filesystem::path{WIOLA_TEST_DATA_DIR} / name);
}

TEST(OggQualification, VorbisAndOpusDecodeThroughOneProviderWithCorrectRates)
{
    auto provider{wiola::codec::ffmpeg::make_provider()};
    for (const auto& [name, rate, frames] : {
             // Lossy qualification follows FFmpeg's independent decoded timeline.
             std::tuple{"tone-vorbis.ogg", 44100, 10897},
             std::tuple{"tone.opus",       48000, 12000},
    }) {
        auto asset{provider->open(
            wiola::media::local_resource(std::filesystem::path{WIOLA_TEST_DATA_DIR} / name)
        )};

        ASSERT_TRUE(asset) << name;

        auto decoder{(*asset)->create_decoder()};

        ASSERT_TRUE(decoder) << name;
        EXPECT_EQ((*decoder)->spec().sample_rate, wiola::units::Frequency{rate}) << name;

        auto samples{read_all(**decoder)};

        ASSERT_TRUE(samples) << name;
        EXPECT_EQ(samples->size(), static_cast<std::size_t>(frames) * 2U) << name;

        auto metadata{(*asset)->read_metadata()};

        ASSERT_TRUE(metadata) << name;
        EXPECT_EQ(metadata->track.artist, "Исполнитель") << name;
        EXPECT_FALSE(metadata->track.title.empty()) << name;
    }
}

TEST(OggQualification, SeeksMatchSequentialVorbisAndOpusSamples)
{
    for (const char* name : {"tone-vorbis.ogg", "tone.opus"}) {
        auto reference{open_ogg(name)};
        auto sought{open_ogg(name)};

        ASSERT_TRUE(reference) << name;
        ASSERT_TRUE(sought) << name;

        auto expected{read_all(**reference)};

        ASSERT_TRUE(expected) << name;
        ASSERT_TRUE((*sought)->seek(wiola::pcm::Frames{1000})) << name;

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

TEST(OggQualification, ChainedIdenticalPcmKeepsEverySampleAcrossTheLinkBoundary)
{
    auto reference{open_ogg("tone-vorbis.ogg")};
    auto chained{open_ogg("chained-same-shape.ogg")};

    ASSERT_TRUE(reference);
    ASSERT_TRUE(chained);

    auto link{read_all(**reference)};

    ASSERT_TRUE(link);

    std::vector<float> expected{*link};
    expected.insert(expected.end(), link->begin(), link->end());
    EXPECT_EQ((*chained)->length_accuracy(), wiola::media::LengthAccuracy::estimated);
    EXPECT_EQ((*chained)->num_frames(), (*reference)->num_frames());

    auto samples{read_all(**chained)};

    ASSERT_TRUE(samples);
    ASSERT_EQ(samples->size(), expected.size());

    for (std::size_t index{0}; index < expected.size(); ++index)
        EXPECT_FLOAT_EQ((*samples)[index], expected[index]) << "sample " << index;
}

TEST(OggQualification, RejectsChainedPcmShapeChanges)
{
    for (const char* name : {"chained-rate-change.ogg", "chained-channel-change.ogg"}) {
        auto decoder{open_ogg(name)};

        ASSERT_TRUE(decoder) << name;

        auto samples{read_all(**decoder)};

        ASSERT_FALSE(samples) << name;
        EXPECT_EQ(samples.error().category, wiola::media::ErrorCategory::unsupported) << name;
    }
}

} // namespace
