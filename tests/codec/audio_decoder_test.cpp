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

#include <codec/decode/registry.hpp>

#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <functional>
#include <utility>

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

wiola::codec::DecoderStreamInfo exact_stream(wiola::pcm::Frames length)
{
    return {
        .spec = {},
        .length = length,
        .length_accuracy = wiola::codec::LengthAccuracy::exact,
        .seekable = true,
    };
}

class CallbackDecoder final : public wiola::codec::AudioDecoder {
public:
    using Decode =
        std::function<wiola::media::Result<wiola::codec::DecodeOutcome>(wiola::pcm::Frames)>;
    using Seek = std::function<wiola::media::Result<wiola::codec::SeekOutcome>(wiola::pcm::Frames)>;

    CallbackDecoder(wiola::codec::DecoderStreamInfo info, Decode decode, Seek seek = {})
        : AudioDecoder{info}
        , decode_{std::move(decode)}
        , seek_{std::move(seek)}
    {
        if (!seek_) {
            seek_ = [](wiola::pcm::Frames target) {
                return wiola::codec::SeekOutcome{
                    .target = target,
                    .achieved = target,
                };
            };
        }
    }

protected:
    wiola::media::Result<wiola::codec::DecodeOutcome> decode(std::span<float>,
        wiola::pcm::Frames wanted) override
    {
        return decode_(wanted);
    }

    wiola::media::Result<wiola::codec::SeekOutcome> seek_frame(wiola::pcm::Frames target) override
    {
        return seek_(target);
    }

private:
    Decode decode_;
    Seek seek_;
};

TEST(AudioDecoder, RejectsAnOutputSpanThatEndsInsideAFrame)
{
    bool called{false};

    CallbackDecoder decoder{
        exact_stream(wiola::pcm::Frames{4}),
        [&called](wiola::pcm::Frames) {
            called = true;

            return wiola::codec::DecodeOutcome{};
        },
    };
    std::array<float, 3> output{};

    auto read{decoder.read(output)};

    ASSERT_FALSE(read);
    EXPECT_EQ(read.error().category, wiola::media::ErrorCategory::read_failed);
    EXPECT_FALSE(called);
}

TEST(AudioDecoder, AnEmptyExactStreamIsAlreadyExhausted)
{
    CallbackDecoder decoder{
        exact_stream(wiola::pcm::Frames{}),
        [](wiola::pcm::Frames) -> wiola::media::Result<wiola::codec::DecodeOutcome> {
            ADD_FAILURE() << "an empty stream must not call its backend";

            return wiola::codec::DecodeOutcome{};
        },
    };
    std::array<float, 2> output{};

    auto read{decoder.read(output)};
    auto repeated{decoder.read(output)};

    ASSERT_TRUE(read);
    ASSERT_TRUE(repeated);

    EXPECT_TRUE(read->end_of_stream);
    EXPECT_TRUE(repeated->end_of_stream);
    EXPECT_TRUE(decoder.exhausted());
    EXPECT_EQ(decoder.stream_info().length, wiola::pcm::Frames{});
}

TEST(AudioDecoder, RejectsBackendOverproduction)
{
    CallbackDecoder decoder{
        exact_stream(wiola::pcm::Frames{4}),
        [](wiola::pcm::Frames wanted) {
            return wiola::codec::DecodeOutcome{
                .frames = wanted + wiola::pcm::Frames{1},
            };
        },
    };
    std::array<float, 2> output{};

    auto read{decoder.read(output)};

    ASSERT_FALSE(read);
    EXPECT_EQ(read.error().category, wiola::media::ErrorCategory::damaged);
}

TEST(AudioDecoder, RejectsBackendProgressStalls)
{
    CallbackDecoder decoder{
        exact_stream(wiola::pcm::Frames{4}),
        [](wiola::pcm::Frames) { return wiola::codec::DecodeOutcome{}; },
    };
    std::array<float, 2> output{};

    auto read{decoder.read(output)};

    ASSERT_FALSE(read);
    EXPECT_EQ(read.error().category, wiola::media::ErrorCategory::damaged);
}

TEST(AudioDecoder, RenderPropagatesTheBackendFailure)
{
    CallbackDecoder decoder{
        exact_stream(wiola::pcm::Frames{4}),
        [](wiola::pcm::Frames) -> wiola::media::Result<wiola::codec::DecodeOutcome> {
            return wiola::media::unexpected{
                wiola::media::Error{
                                    .category = wiola::media::ErrorCategory::damaged,
                                    .detail = "injected decode failure",
                                    },
            };
        },
    };
    std::array<float, 2> output{};

    auto rendered{decoder.render(output)};

    ASSERT_FALSE(rendered);
    EXPECT_EQ(rendered.error().category, wiola::media::ErrorCategory::damaged);
}

TEST(AudioDecoder, RejectsSeekingAnUnseekableStream)
{
    auto info{exact_stream(wiola::pcm::Frames{4})};
    info.seekable = false;

    CallbackDecoder decoder{
        info,
        [](wiola::pcm::Frames) {
            return wiola::codec::DecodeOutcome{
                .frames = {},
                .end_of_stream = true,
            };
        },
    };

    auto sought{decoder.seek(wiola::pcm::Frames{1})};

    ASSERT_FALSE(sought);
    EXPECT_EQ(sought.error().category, wiola::media::ErrorCategory::seek_failed);
}

TEST(AudioDecoder, RejectsABackendThatChangesTheSeekTarget)
{
    CallbackDecoder decoder{
        exact_stream(wiola::pcm::Frames{4}),
        [](wiola::pcm::Frames) { return wiola::codec::DecodeOutcome{}; },
        [](wiola::pcm::Frames target) {
            return wiola::codec::SeekOutcome{
                .target = target + wiola::pcm::Frames{1},
                .achieved = target,
            };
        },
    };

    auto sought{decoder.seek(wiola::pcm::Frames{1})};

    ASSERT_FALSE(sought);
    EXPECT_EQ(sought.error().category, wiola::media::ErrorCategory::seek_failed);
}

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

        EXPECT_GT(total, wiola::pcm::Frames{}) << name;
        if ((*decoder)->length_accuracy() == wiola::codec::LengthAccuracy::exact)
            EXPECT_EQ(total, (*decoder)->num_frames()) << name;
    }
}

} // namespace
