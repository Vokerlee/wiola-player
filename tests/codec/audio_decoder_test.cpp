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
    wiola::media::Result<wiola::codec::DecodeOutcome> decode(std::span<float>,
        wiola::pcm::Frames) override
    {
        return wiola::codec::DecodeOutcome{.frames = wiola::pcm::Frames{1}};
    }

    wiola::media::Result<wiola::codec::SeekOutcome>
    seek_frame(wiola::pcm::Frames frame_index) override
    {
        return wiola::codec::SeekOutcome{.target = frame_index, .achieved = frame_index};
    }
};

class CorruptTailDecoder final : public wiola::codec::AudioDecoder {
public:
    CorruptTailDecoder()
        : AudioDecoder{{}, wiola::pcm::Frames{4}}
    {
    }

protected:
    wiola::media::Result<wiola::codec::DecodeOutcome> decode(std::span<float>,
        wiola::pcm::Frames) override
    {
        if (position_frames() == wiola::pcm::Frames{})
            return wiola::codec::DecodeOutcome{.frames = wiola::pcm::Frames{2}};

        return wiola::media::unexpected{
            wiola::media::Error{.category = wiola::media::ErrorCategory::damaged,
                                .detail = "corrupt tail"}
        };
    }

    wiola::media::Result<wiola::codec::SeekOutcome>
    seek_frame(wiola::pcm::Frames frame_index) override
    {
        return wiola::codec::SeekOutcome{.target = frame_index, .achieved = frame_index};
    }
};

class EstimatedDecoder final : public wiola::codec::AudioDecoder {
public:
    EstimatedDecoder()
        : AudioDecoder{
              wiola::codec::DecoderStreamInfo{.spec = {},
                                              .length = wiola::pcm::Frames{2},
                                              .length_accuracy = wiola::codec::LengthAccuracy::estimated}
    }
    {
    }

protected:
    wiola::media::Result<wiola::codec::DecodeOutcome> decode(std::span<float>,
        wiola::pcm::Frames wanted) override
    {
        if (position_frames() == wiola::pcm::Frames{4})
            return wiola::codec::DecodeOutcome{.frames = {}, .end_of_stream = true};

        return wiola::codec::DecodeOutcome{.frames = wanted};
    }

    wiola::media::Result<wiola::codec::SeekOutcome>
    seek_frame(wiola::pcm::Frames frame_index) override
    {
        return wiola::codec::SeekOutcome{.target = frame_index, .achieved = frame_index};
    }
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

TEST(AudioDecoder, EstimatedLengthDoesNotClampOutputOrCauseFalseDamage)
{
    EstimatedDecoder decoder;
    std::array<float, 4> output{};

    auto first{decoder.read(output)};
    ASSERT_TRUE(first);
    EXPECT_EQ(first->frames, wiola::pcm::Frames{2});
    EXPECT_FALSE(first->end_of_stream);
    EXPECT_EQ(decoder.position_frames(), wiola::pcm::Frames{2});

    auto beyond_estimate{decoder.read(output)};
    ASSERT_TRUE(beyond_estimate);
    EXPECT_EQ(beyond_estimate->frames, wiola::pcm::Frames{2});
    EXPECT_FALSE(beyond_estimate->end_of_stream);
    EXPECT_EQ(decoder.position_frames(), wiola::pcm::Frames{4});
    EXPECT_EQ(decoder.num_frames_left(), wiola::pcm::Frames{});

    auto end{decoder.read(output)};
    ASSERT_TRUE(end);
    EXPECT_EQ(end->frames, wiola::pcm::Frames{});
    EXPECT_TRUE(end->end_of_stream);
    EXPECT_TRUE(decoder.exhausted());
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
