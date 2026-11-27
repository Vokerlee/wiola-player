/**
 * @file
 * @brief Tests for playing one decoder through a device.
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

#include <buffering/buffer_source.hpp>
#include <buffering/pcm_queue.hpp>
#include <fakes/output.hpp>
#include <fixtures/temporary_directory.hpp>
#include <fixtures/wav.hpp>
#include <transport/player.hpp>
#include <wiola/media/codecs/ffmpeg/composition.hpp>

#include <gtest/gtest.h>

#include <utility>

TEST(Player, PlaysToTheEndOnItsOwn)
{
    const wiola::testing::TemporaryDirectory files;
    const auto path =
        wiola::testing::write_wav("track.wav", wiola::testing::source_rate, files.path());
    auto opened = wiola::codec::ffmpeg::make_registry().open(path);

    ASSERT_TRUE(opened);

    auto source = std::move(opened.value());

    ASSERT_NE(source, nullptr);

    const auto* decoder{source.get()};
    wiola::pcm::PcmQueue queue{source->spec(), wiola::pcm::Frames{512}};
    wiola::audio::BufferSource decoded{source->spec(), queue.consumer()};
    wiola::testing::FakeOutput output{decoded};
    wiola::engine::Player player{std::move(source), queue.producer(), output};

    ASSERT_TRUE(player.start());

    player.wait();

    EXPECT_EQ(player.state(), wiola::engine::PlaybackState::ended);
    EXPECT_TRUE(decoder->exhausted());
    EXPECT_FALSE(output.running());
}
