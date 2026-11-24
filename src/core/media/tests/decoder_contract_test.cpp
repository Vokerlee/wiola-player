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

#include <wiola/media/audio_decoder.hpp>

#include <gtest/gtest.h>

#include <array>
#include <functional>
#include <span>
#include <utility>

namespace {

class ShortDecoder final : public wiola::media::AudioDecoder {
public:
    ShortDecoder()
        : AudioDecoder{{}, wiola::pcm::Frames{3}}
    {
    }

protected:
    wiola::media::Result<wiola::media::DecodeOutcome> decode(std::span<float>,
        wiola::pcm::Frames) override
    {
        return wiola::media::DecodeOutcome{.frames = wiola::pcm::Frames{1}};
    }

    wiola::media::Result<wiola::media::SeekOutcome>
    seek_frame(wiola::pcm::Frames frame_index) override
    {
        return wiola::media::SeekOutcome{.target = frame_index, .achieved = frame_index};
    }
};

class CorruptTailDecoder final : public wiola::media::AudioDecoder {
public:
    CorruptTailDecoder()
        : AudioDecoder{{}, wiola::pcm::Frames{4}}
    {
    }

protected:
    wiola::media::Result<wiola::media::DecodeOutcome> decode(std::span<float>,
        wiola::pcm::Frames) override
    {
        if (position_frames() == wiola::pcm::Frames{})
            return wiola::media::DecodeOutcome{.frames = wiola::pcm::Frames{2}};

        return wiola::media::unexpected{
            wiola::media::Error{.category = wiola::media::ErrorCategory::damaged,
                                .detail = "corrupt tail"}
        };
    }

    wiola::media::Result<wiola::media::SeekOutcome>
    seek_frame(wiola::pcm::Frames frame_index) override
    {
        return wiola::media::SeekOutcome{.target = frame_index, .achieved = frame_index};
    }
};

class EstimatedDecoder final : public wiola::media::AudioDecoder {
public:
    EstimatedDecoder()
        : AudioDecoder{
              wiola::media::DecoderStreamInfo{.spec = {},
                                              .length = wiola::pcm::Frames{2},
                                              .length_accuracy = wiola::media::LengthAccuracy::estimated}
    }
    {
    }

protected:
    wiola::media::Result<wiola::media::DecodeOutcome> decode(std::span<float>,
        wiola::pcm::Frames wanted) override
    {
        if (position_frames() == wiola::pcm::Frames{4})
            return wiola::media::DecodeOutcome{.frames = {}, .end_of_stream = true};

        return wiola::media::DecodeOutcome{.frames = wanted};
    }

    wiola::media::Result<wiola::media::SeekOutcome>
    seek_frame(wiola::pcm::Frames frame_index) override
    {
        return wiola::media::SeekOutcome{.target = frame_index, .achieved = frame_index};
    }
};

wiola::media::DecoderStreamInfo exact_stream(wiola::pcm::Frames length)
{
    return {
        .spec = {},
        .length = length,
        .length_accuracy = wiola::media::LengthAccuracy::exact,
        .seekable = true,
    };
}

class CallbackDecoder final : public wiola::media::AudioDecoder {
public:
    using Decode =
        std::function<wiola::media::Result<wiola::media::DecodeOutcome>(wiola::pcm::Frames)>;
    using Seek = std::function<wiola::media::Result<wiola::media::SeekOutcome>(wiola::pcm::Frames)>;

    CallbackDecoder(wiola::media::DecoderStreamInfo info, Decode decode, Seek seek = {})
        : AudioDecoder{info}
        , decode_{std::move(decode)}
        , seek_{std::move(seek)}
    {
        if (!seek_) {
            seek_ = [](wiola::pcm::Frames target) {
                return wiola::media::SeekOutcome{
                    .target = target,
                    .achieved = target,
                };
            };
        }
    }

protected:
    wiola::media::Result<wiola::media::DecodeOutcome> decode(std::span<float>,
        wiola::pcm::Frames wanted) override
    {
        return decode_(wanted);
    }

    wiola::media::Result<wiola::media::SeekOutcome> seek_frame(wiola::pcm::Frames target) override
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

            return wiola::media::DecodeOutcome{};
        },
    };
    std::array<float, 3> output{};

    auto read{decoder.read(output)};

    ASSERT_FALSE(read);
    EXPECT_EQ(read.error().category, wiola::media::ErrorCategory::read_failed);
    EXPECT_FALSE(called);
}

TEST(AudioDecoder, EmptyOutputDoesNotExhaustAStreamWithFramesRemaining)
{
    using wiola::media::LengthAccuracy;
    for (const auto accuracy :
        std::array{LengthAccuracy::exact, LengthAccuracy::estimated, LengthAccuracy::unknown}) {
        SCOPED_TRACE(static_cast<int>(accuracy));
        auto info{exact_stream(wiola::pcm::Frames{2})};
        info.length_accuracy = accuracy;
        if (accuracy == LengthAccuracy::unknown)
            info.length.reset();
        int calls{0};
        CallbackDecoder decoder{info, [&calls](wiola::pcm::Frames wanted) {
                                    ++calls;
                                    return wiola::media::DecodeOutcome{.frames = wanted};
                                }};

        auto empty{decoder.read(std::span<float>{})};
        ASSERT_TRUE(empty);
        EXPECT_EQ(empty->frames, wiola::pcm::Frames{});
        EXPECT_FALSE(empty->end_of_stream);
        EXPECT_FALSE(decoder.exhausted());
        EXPECT_EQ(decoder.position_frames(), wiola::pcm::Frames{});
        auto rendered{decoder.render(std::span<float>{})};
        ASSERT_TRUE(rendered);
        EXPECT_EQ(*rendered, 0);
        EXPECT_FALSE(decoder.exhausted());
        EXPECT_EQ(calls, 0);

        std::array<float, 4> output{};
        auto next{decoder.read(output)};
        ASSERT_TRUE(next);
        EXPECT_EQ(next->frames, wiola::pcm::Frames{2});
        EXPECT_EQ(next->end_of_stream, accuracy == LengthAccuracy::exact);
        EXPECT_EQ(calls, 1);
    }
}

TEST(AudioDecoder, EmptyOutputReportsAnAlreadyEmptyExactStream)
{
    bool called{false};
    CallbackDecoder decoder{exact_stream(wiola::pcm::Frames{}), [&called](wiola::pcm::Frames) {
                                called = true;
                                return wiola::media::DecodeOutcome{};
                            }};
    auto empty{decoder.read(std::span<float>{})};
    ASSERT_TRUE(empty);
    EXPECT_EQ(empty->frames, wiola::pcm::Frames{});
    EXPECT_TRUE(empty->end_of_stream);
    EXPECT_TRUE(decoder.exhausted());
    EXPECT_EQ(decoder.position_frames(), wiola::pcm::Frames{});
    auto repeated{decoder.render(std::span<float>{})};
    ASSERT_TRUE(repeated);
    EXPECT_EQ(*repeated, 0);
    EXPECT_FALSE(called);
}

TEST(AudioDecoder, AnEmptyExactStreamIsAlreadyExhausted)
{
    CallbackDecoder decoder{
        exact_stream(wiola::pcm::Frames{}),
        [](wiola::pcm::Frames) -> wiola::media::Result<wiola::media::DecodeOutcome> {
            ADD_FAILURE() << "an empty stream must not call its backend";

            return wiola::media::DecodeOutcome{};
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
            return wiola::media::DecodeOutcome{
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
        [](wiola::pcm::Frames) { return wiola::media::DecodeOutcome{}; },
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
        [](wiola::pcm::Frames) -> wiola::media::Result<wiola::media::DecodeOutcome> {
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
            return wiola::media::DecodeOutcome{
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
        [](wiola::pcm::Frames) { return wiola::media::DecodeOutcome{}; },
        [](wiola::pcm::Frames target) {
            return wiola::media::SeekOutcome{
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

TEST(AudioDecoder, RejectsPastExactLengthBeforeCallingTheBackend)
{
    int seeks{0};
    CallbackDecoder decoder{
        exact_stream(wiola::pcm::Frames{4}),
        [](wiola::pcm::Frames) { return wiola::media::DecodeOutcome{}; },
        [&seeks](wiola::pcm::Frames target) {
            ++seeks;
            return wiola::media::SeekOutcome{.target = target, .achieved = target};
        },
    };

    const auto sought{decoder.seek(wiola::pcm::Frames{5})};

    ASSERT_FALSE(sought);
    EXPECT_EQ(sought.error().category, wiola::media::ErrorCategory::invalid_seek);
    EXPECT_EQ(seeks, 0);
    EXPECT_EQ(decoder.position_frames(), wiola::pcm::Frames{});
    EXPECT_FALSE(decoder.exhausted());
}

TEST(AudioDecoder, SeekingToExactEndAndBackUpdatesExhaustion)
{
    CallbackDecoder decoder{
        exact_stream(wiola::pcm::Frames{4}),
        [](wiola::pcm::Frames) { return wiola::media::DecodeOutcome{}; },
    };

    EXPECT_EQ(decoder.duration_frames(), wiola::pcm::Frames{4});
    ASSERT_TRUE(decoder.seek(wiola::pcm::Frames{4}));
    EXPECT_TRUE(decoder.exhausted());
    EXPECT_EQ(decoder.position_frames(), wiola::pcm::Frames{4});
    ASSERT_TRUE(decoder.seek(wiola::pcm::Frames{2}));
    EXPECT_FALSE(decoder.exhausted());
    EXPECT_EQ(decoder.position_frames(), wiola::pcm::Frames{2});
}

TEST(AudioDecoder, AllowsSeekBeyondEstimatedOrUnknownLength)
{
    for (const bool estimated : {true, false}) {
        auto info{exact_stream(wiola::pcm::Frames{1})};
        info.length_accuracy = estimated ? wiola::media::LengthAccuracy::estimated
                                         : wiola::media::LengthAccuracy::unknown;
        if (!estimated)
            info.length.reset();

        CallbackDecoder decoder{
            info,
            [](wiola::pcm::Frames) { return wiola::media::DecodeOutcome{}; },
        };

        EXPECT_EQ(decoder.duration_frames(), info.length);
        const auto sought{decoder.seek(wiola::pcm::Frames{8})};
        ASSERT_TRUE(sought);
        EXPECT_EQ(sought->achieved, wiola::pcm::Frames{8});
        EXPECT_EQ(decoder.position_frames(), wiola::pcm::Frames{8});
        EXPECT_FALSE(decoder.exhausted());
    }
}

TEST(AudioDecoder, RenderReportsInterleavedSamplesRatherThanFrames)
{
    CallbackDecoder decoder{
        exact_stream(wiola::pcm::Frames{2}),
        [](wiola::pcm::Frames frames) { return wiola::media::DecodeOutcome{.frames = frames}; },
    };
    std::array<float, 8> output{};

    const auto rendered{decoder.render(output)};

    ASSERT_TRUE(rendered);
    EXPECT_EQ(*rendered, 4);
    EXPECT_EQ(decoder.position_frames(), wiola::pcm::Frames{2});
    EXPECT_TRUE(decoder.exhausted());
}

TEST(AudioDecoder, ARejectedBackendSeekPreservesTheCommittedPosition)
{
    CallbackDecoder decoder{
        exact_stream(wiola::pcm::Frames{4}),
        [](wiola::pcm::Frames) { return wiola::media::DecodeOutcome{}; },
        [](wiola::pcm::Frames target) -> wiola::media::Result<wiola::media::SeekOutcome> {
            if (target == wiola::pcm::Frames{1})
                return wiola::media::unexpected{
                    wiola::media::Error{.category = wiola::media::ErrorCategory::seek_failed,
                                        .detail = "controlled seek failure"}
                };
            return wiola::media::SeekOutcome{.target = target, .achieved = target};
        },
    };
    ASSERT_TRUE(decoder.seek(wiola::pcm::Frames{2}));

    const auto refused{decoder.seek(wiola::pcm::Frames{1})};

    ASSERT_FALSE(refused);
    EXPECT_EQ(refused.error().category, wiola::media::ErrorCategory::seek_failed);
    EXPECT_EQ(decoder.position_frames(), wiola::pcm::Frames{2});
    EXPECT_FALSE(decoder.exhausted());
}

} // namespace
