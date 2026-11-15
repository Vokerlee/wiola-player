/**
 * @file
 * @brief Shared contract tests for typed audio decoder outcomes.
 * @author Roman Glaz
 * @copyright © 2026, <vokerlee@gmail.com>
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

#include <codec/decode/registry.hpp>

#include <gtest/gtest.h>

#include <array>
#include <filesystem>

namespace {

class ShortDecoder final : public wiola::codec::AudioDecoder {
public:
    ShortDecoder()
        : AudioDecoder{{}, wiola::pcm::Frames{3}}
    {
    }

protected:
    wiola::media::Result<wiola::pcm::Frames> decode(std::span<float>, wiola::pcm::Frames) override
    {
        return wiola::pcm::Frames{1};
    }

    bool seek_frame(wiola::pcm::Frames) override { return true; }
};

class CorruptTailDecoder final : public wiola::codec::AudioDecoder {
public:
    CorruptTailDecoder()
        : AudioDecoder{{}, wiola::pcm::Frames{4}}
    {
    }

protected:
    wiola::media::Result<wiola::pcm::Frames> decode(std::span<float>, wiola::pcm::Frames) override
    {
        if (num_frames_read() == wiola::pcm::Frames{})
            return wiola::pcm::Frames{2};

        return wiola::media::unexpected{
            wiola::media::Error{.category = wiola::media::ErrorCategory::damaged,
                                .detail = "corrupt tail"}
        };
    }

    bool seek_frame(wiola::pcm::Frames) override { return true; }

private:
    wiola::pcm::Frames num_frames_read() const { return num_frames() - num_frames_left(); }
};

TEST(AudioDecoder, AShortReadIsProgressRatherThanEndOfFile)
{
    ShortDecoder decoder;
    std::array<float, 6> output{};

    auto first{decoder.read(output)};
    ASSERT_TRUE(first);
    EXPECT_EQ(first->frames, wiola::pcm::Frames{1});
    EXPECT_FALSE(first->end_of_stream);

    auto second{decoder.read(output)};
    ASSERT_TRUE(second);
    EXPECT_EQ(second->frames, wiola::pcm::Frames{1});
    EXPECT_FALSE(second->end_of_stream);

    auto third{decoder.read(output)};
    ASSERT_TRUE(third);
    EXPECT_EQ(third->frames, wiola::pcm::Frames{1});
    EXPECT_TRUE(third->end_of_stream);
}

TEST(AudioDecoder, ACorruptTailIsAFailureRatherThanEndOfFile)
{
    CorruptTailDecoder decoder;
    std::array<float, 4> output{};

    ASSERT_TRUE(decoder.read(output));
    auto tail{decoder.read(output)};

    ASSERT_FALSE(tail);
    EXPECT_EQ(tail.error().category, wiola::media::ErrorCategory::damaged);
    EXPECT_EQ(decoder.num_frames_left(), wiola::pcm::Frames{2});
}

TEST(AudioDecoder, EveryBackendReportsWholeFrameProgressAndExplicitEnd)
{
    for (const char* name : {"tone.wav", "tone.flac", "tone.mp3"}) {
        auto decoder{wiola::codec::MediaBackendRegistry{}.open(
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

        EXPECT_EQ(total, (*decoder)->num_frames()) << name;
    }
}

} // namespace
