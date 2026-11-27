/**
 * @file
 * @brief Unit tests for the PCM stream description.
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

#include <wiola/media/stream_spec.hpp>
#include <wiola/units.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>

namespace {

using wiola::pcm::Frames;
using wiola::pcm::StreamSpec;
using namespace wiola::units;

TEST(StreamSpec, DefaultsToStereoAt48kHz)
{
    constexpr StreamSpec spec{};

    static_assert(spec.valid());
    EXPECT_EQ(spec.sample_rate, 48_kHz);
    EXPECT_EQ(spec.num_channels, 2u);
}

TEST(StreamSpec, ConvertsBetweenFramesAndSamples)
{
    constexpr StreamSpec spec{.sample_rate = 44.1_kHz, .num_channels = 2};

    EXPECT_EQ(spec.samples_per(Frames{128}), 256u);
    EXPECT_EQ(spec.frames_per(256), Frames{128});
    EXPECT_EQ(spec.frames_per(spec.samples_per(Frames{7})), Frames{7});
}

TEST(StreamSpec, SaysHowLongFramesTakeToPlay)
{
    constexpr StreamSpec spec{.sample_rate = 44.1_kHz, .num_channels = 2};

    static_assert(spec.time_per(Frames{44100}) == 1_s);

    EXPECT_EQ(spec.time_per(Frames{}), Time{});
    EXPECT_EQ(spec.time_per(Frames{22050}), 500_ms);
    EXPECT_EQ(spec.time_per(Frames{44100}), 1_s);
}

/// The way back from a duration, so that a position and a length are measured the same way.
TEST(StreamSpec, TimeAndSamplesAgreeWithEachOther)
{
    constexpr StreamSpec spec{.sample_rate = 48_kHz, .num_channels = 2};

    EXPECT_EQ(spec.time_per(spec.frames_per(spec.samples_per(250_ms))), 250_ms);
    EXPECT_EQ(spec.samples_per(spec.time_per(Frames{1200})), 2400u);
}

TEST(StreamSpec, SizesABufferFromMilliseconds)
{
    constexpr StreamSpec spec{.sample_rate = 48_kHz, .num_channels = 2};

    EXPECT_EQ(spec.samples_per(1_s), 96000u);
    EXPECT_EQ(spec.samples_per(250_ms), 24000u);
}

TEST(StreamSpec, RejectsEmptyStreams)
{
    EXPECT_FALSE((StreamSpec{.sample_rate = 0_Hz, .num_channels = 2}).valid());
    EXPECT_FALSE((StreamSpec{.sample_rate = 48_kHz, .num_channels = 0}).valid());
}

TEST(StreamSpec, RejectsNonFiniteRates)
{
    const std::array rates{Frequency{std::numeric_limits<double>::quiet_NaN()},
        Frequency{std::numeric_limits<double>::infinity()}};

    for (const Frequency rate : rates) {
        const StreamSpec spec{.sample_rate = rate, .num_channels = 2};

        EXPECT_FALSE(spec.valid());
        EXPECT_FALSE(spec.checked_frames_at(1_s).has_value());
        EXPECT_FALSE(spec.checked_samples_per(Frames{1}).has_value());
    }
}

TEST(StreamSpec, RejectsNonFiniteAndNegativeTimes)
{
    constexpr StreamSpec spec{};
    const std::array times{Time{std::numeric_limits<double>::quiet_NaN()},
        Time{std::numeric_limits<double>::infinity()},
        Time{-std::numeric_limits<double>::infinity()}, Time{-1.0}};

    for (const Time time : times) {
        EXPECT_FALSE(spec.checked_frames_at(time).has_value());
        EXPECT_FALSE(spec.checked_samples_per(time).has_value());
    }
}

TEST(StreamSpec, DetectsFrameToSampleOverflow)
{
    constexpr StreamSpec stereo{};
    constexpr std::size_t largest{std::numeric_limits<std::size_t>::max()};

    EXPECT_EQ(stereo.checked_samples_per(Frames{largest / 2}), largest - 1);
    EXPECT_FALSE(stereo.checked_samples_per(Frames{largest}).has_value());

    const StreamSpec many_channels{.sample_rate = 48_kHz, .num_channels = largest};

    EXPECT_FALSE(many_channels.checked_samples_per(Frames{2}).has_value());
}

TEST(StreamSpec, DetectsDurationConversionOverflow)
{
    constexpr StreamSpec spec{};

    EXPECT_FALSE(spec.checked_frames_at(Time{std::numeric_limits<double>::max()}).has_value());

    const StreamSpec extreme_rate{.sample_rate = Frequency{std::numeric_limits<double>::max()},
        .num_channels = 2};

    EXPECT_FALSE(extreme_rate.checked_frames_at(1_s).has_value());
    EXPECT_FALSE(extreme_rate.checked_samples_per(1_s).has_value());
}

TEST(StreamSpec, ChecksDurationConversionBeforeMultiplyingChannels)
{
    const StreamSpec spec{.sample_rate = 48_kHz,
        .num_channels = std::numeric_limits<std::size_t>::max()};

    EXPECT_FALSE(spec.checked_samples_per(1_s).has_value());
    EXPECT_EQ(spec.checked_samples_per(Time{}), 0U);
}

TEST(StreamSpec, ComparesMemberwise)
{
    constexpr StreamSpec stereo{.sample_rate = 48_kHz, .num_channels = 2};
    constexpr StreamSpec mono{.sample_rate = 48_kHz, .num_channels = 1};

    EXPECT_EQ(stereo, (StreamSpec{}));
    EXPECT_NE(stereo, mono);
}

} // namespace
