/**
 * @file
 * @brief Shared contract tests for typed audio decoder outcomes.
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
#include <wiola/media/codecs/registry.hpp>

#include <gtest/gtest.h>

#include <array>
#include <filesystem>

namespace {

TEST(AudioDecoder, EveryBackendReportsWholeFrameProgressAndExplicitEnd)
{
    for (const char* name : {"tone.wav", "tone.flac", "tone.mp3"}) {
        auto decoder{wiola::codec::ffmpeg::make_registry().open(
            std::filesystem::path{WIOLA_TEST_DATA_DIR} / name
        )};
        ASSERT_TRUE(decoder) << name;

        std::array<float, 1000> output{};
        wiola::pcm::Frames total{};
        bool ended{false};

        while (!ended) {
            auto read{(*decoder)->read(output)};
            ASSERT_TRUE(read) << name;
            EXPECT_LE(read->frames, wiola::pcm::Frames{500}) << name;
            total += read->frames;
            ended = read->end_of_stream;
        }

        EXPECT_GT(total, wiola::pcm::Frames{}) << name;
        if ((*decoder)->length_accuracy() == wiola::media::LengthAccuracy::exact)
            EXPECT_EQ(total, (*decoder)->num_frames()) << name;
    }
}

} // namespace
