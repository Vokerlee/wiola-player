/**
 * @file
 * @brief Unit tests for frame-aligned PCM queueing.
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

#include <buffering/pcm_queue.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>

namespace {

using wiola::pcm::Frames;
using wiola::pcm::PcmQueue;
using wiola::pcm::StreamSpec;
using wiola::units::Frequency;

constexpr StreamSpec three_channels{.sample_rate = Frequency{48000.0}, .num_channels = 3};

std::vector<float> counted(std::size_t count)
{
    std::vector<float> samples(count);
    for (std::size_t i = 0; i < count; ++i)
        samples[i] = static_cast<float>(i);

    return samples;
}

TEST(PcmQueue, ChecksFrameCapacityAndRounding)
{
    const auto capacity{PcmQueue::checked_capacity(three_channels, Frames{4})};
    ASSERT_TRUE(capacity);
    EXPECT_GE(*capacity, three_channels.samples_per(Frames{4}));
    EXPECT_FALSE(PcmQueue::checked_capacity(three_channels,
        Frames{std::numeric_limits<std::size_t>::max()})
            .has_value());

    const StreamSpec invalid{.sample_rate = Frequency{48000.0}, .num_channels = 0};
    EXPECT_FALSE(PcmQueue::checked_capacity(invalid, Frames{4}).has_value());
    EXPECT_THROW(PcmQueue(invalid, Frames{4}), std::invalid_argument);
}

TEST(PcmQueue, ZeroMinimumStillTransfersACompleteFrameForEveryChannelShape)
{
    for (const std::size_t channels : {1U, 2U, 3U, 8U}) {
        const StreamSpec spec{.sample_rate = Frequency{48000.0}, .num_channels = channels};
        PcmQueue queue{spec, Frames{}};
        auto producer{queue.producer()};
        auto consumer{queue.consumer()};
        const auto input{counted(channels)};
        std::vector<float> output(channels, -1.0F);

        ASSERT_EQ(producer.push(input), input.size()) << "channel count " << channels;
        ASSERT_EQ(consumer.pop(output), output.size());
        EXPECT_EQ(output, input);
        EXPECT_EQ(queue.size_approx(), 0U);
    }
}

TEST(PcmQueue, ZeroMinimumCannotHideAnUnrepresentableCompleteFrame)
{
    const StreamSpec spec{.sample_rate = Frequency{48000.0},
        .num_channels = std::numeric_limits<std::size_t>::max()};

    EXPECT_FALSE(PcmQueue::checked_capacity(spec, Frames{}));
    EXPECT_THROW((PcmQueue{spec, Frames{}}), std::invalid_argument);
}

TEST(PcmQueue, OddConsumerSpansNeverSplitFrames)
{
    PcmQueue queue{three_channels, Frames{4}};
    auto producer{queue.producer()};
    auto consumer{queue.consumer()};
    const std::vector<float> input{counted(12)};
    ASSERT_EQ(producer.push(input), input.size());

    std::array<float, 5> first{};
    ASSERT_EQ(consumer.pop(first), 3U);
    EXPECT_EQ(first[2], 2.0F);

    std::array<float, 4> second{};
    ASSERT_EQ(consumer.pop(second), 3U);
    EXPECT_EQ(second[0], 3.0F);
    EXPECT_EQ(second[2], 5.0F);
}

TEST(PcmQueue, PreservesChannelOrderAcrossWrapAndShortPush)
{
    PcmQueue queue{three_channels, Frames{4}};
    auto producer{queue.producer()};
    auto consumer{queue.consumer()};
    const std::vector<float> initial{counted(12)};
    ASSERT_EQ(producer.push(initial), initial.size());

    std::array<float, 5> consumed{};
    ASSERT_EQ(consumer.pop(consumed), 3U);

    const std::vector<float> wrapped{12.0F, 13.0F, 14.0F, 15.0F, 16.0F, 17.0F};
    ASSERT_EQ(producer.push(wrapped), wrapped.size());

    std::vector<float> output;
    std::array<float, 5> block{};
    std::size_t num_read{0};
    do {
        num_read = consumer.pop(block);
        output.insert(output.end(), block.begin(), block.begin() + num_read);
    } while (num_read != 0);

    ASSERT_EQ(output.size(), 15U);
    for (std::size_t i = 0; i < output.size(); ++i)
        EXPECT_FLOAT_EQ(output[i], static_cast<float>(i + 3));
}

TEST(PcmQueue, ShortPushEndsAtACompleteFrame)
{
    PcmQueue queue{three_channels, Frames{2}};
    auto producer{queue.producer()};
    auto consumer{queue.consumer()};
    const std::vector<float> two_frames{counted(6)};
    ASSERT_EQ(producer.push(two_frames), 6U);

    std::array<float, 3> one_frame{};
    ASSERT_EQ(consumer.pop(one_frame), 3U);
    ASSERT_EQ(producer.push(two_frames), 3U);
    EXPECT_EQ(queue.size_approx(), 6U);
}

TEST(PcmQueue, InertEndpointsPreserveTheLiveFrameStream)
{
    PcmQueue queue{three_channels, Frames{2}};
    auto producer = queue.producer();
    auto consumer = queue.consumer();
    auto inert_producer = queue.producer();
    auto inert_consumer = queue.consumer();
    const std::array input{1.0F, 2.0F, 3.0F};
    ASSERT_EQ(producer.push(input), input.size());

    EXPECT_EQ(inert_producer.push(input), 0U);
    EXPECT_EQ(inert_producer.space_approx(), 0U);
    inert_producer.mark_discard();
    std::array output{-1.0F, -1.0F, -1.0F};
    EXPECT_EQ(inert_consumer.pop(output), 0U);
    EXPECT_EQ(output.front(), -1.0F);

    ASSERT_EQ(consumer.pop(output), input.size());
    EXPECT_EQ(output, input);
}

TEST(PcmQueue, DiscardReclaimsOldFramesBeforeReadingTheNextStream)
{
    PcmQueue queue{three_channels, Frames{2}};
    auto producer = queue.producer();
    auto consumer = queue.consumer();
    const std::array old_frames{1.0F, 2.0F, 3.0F, 4.0F, 5.0F, 6.0F};
    ASSERT_EQ(producer.push(old_frames), old_frames.size());
    producer.mark_discard();
    EXPECT_EQ(queue.size_approx(), old_frames.size());

    std::array output{-1.0F, -1.0F, -1.0F};
    EXPECT_EQ(consumer.pop(output), 0U);
    EXPECT_EQ(output.front(), -1.0F);
    EXPECT_EQ(queue.size_approx(), 0U);

    const std::array next_frame{7.0F, 8.0F, 9.0F};
    ASSERT_EQ(producer.push(next_frame), next_frame.size());
    ASSERT_EQ(consumer.pop(output), output.size());
    EXPECT_EQ(output, next_frame);
}

} // namespace
