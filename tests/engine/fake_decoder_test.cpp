/**
 * @file
 * @brief Contract tests for the controllable decoder used by engine tests.
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

#include <fakes/decoder.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <thread>

namespace {

using wiola::pcm::Frames;
using wiola::testing::FakeDecoder;
using wiola::testing::Gate;

TEST(FakeDecoder, HoldsAReadUntilTheTestReleasesIt)
{
    FakeDecoder decoder{Frames{8}};
    Gate reading;
    decoder.block_reads_on(reading);
    std::array<float, 16> output{};
    std::size_t num_read{0};

    std::jthread reader{[&] { num_read = decoder.render(output); }};
    reading.wait_until_entered();

    EXPECT_EQ(decoder.num_frames_left(), Frames{8});
    EXPECT_EQ(decoder.num_reads(), 0);

    reading.release();
    reader.join();

    EXPECT_EQ(num_read, output.size());
    EXPECT_EQ(decoder.num_frames_left(), Frames{0});
}

TEST(FakeDecoder, HoldsASeekUntilTheTestReleasesIt)
{
    FakeDecoder decoder{Frames{100}};
    Gate seeking;
    decoder.block_seeks_on(seeking);
    bool sought{false};

    std::jthread seeker{[&] { sought = decoder.seek(Frames{40}); }};
    seeking.wait_until_entered();

    EXPECT_EQ(decoder.last_seek(), Frames{40});
    EXPECT_EQ(decoder.num_frames_left(), Frames{100});

    seeking.release();
    seeker.join();

    EXPECT_TRUE(sought);
    EXPECT_EQ(decoder.num_frames_left(), Frames{60});
}

TEST(FakeDecoder, CanRefuseASeekWithoutMoving)
{
    FakeDecoder decoder{Frames{100}};
    decoder.refuse_seeks();

    EXPECT_FALSE(decoder.seek(Frames{40}));
    EXPECT_EQ(decoder.num_seeks(), 1);
    EXPECT_EQ(decoder.last_seek(), Frames{40});
    EXPECT_EQ(decoder.num_frames_left(), Frames{100});
}

} // namespace
