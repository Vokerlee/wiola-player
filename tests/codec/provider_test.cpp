/**
 * @file
 * @brief Contract tests for the format-neutral local media facade.
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

#include <codec/provider.hpp>

#include <gtest/gtest.h>

#include <array>
#include <filesystem>

namespace {

std::filesystem::path fixture(const char* name)
{
    return std::filesystem::path{WIOLA_TEST_DATA_DIR} / name;
}

TEST(MediaProvider, OpensEverySupportedFormatThroughOneFacade)
{
    wiola::codec::LocalMediaProvider provider;

    for (const char* name : {"tone.wav", "tone.flac", "tone.mp3"}) {
        auto asset{provider.open(fixture(name))};
        ASSERT_TRUE(asset) << name;

        auto info{(*asset)->inspect()};
        ASSERT_TRUE(info) << name;
        EXPECT_EQ(info->spec.sample_rate, wiola::units::Frequency{44100}) << name;
        EXPECT_EQ(info->spec.num_channels, 2U) << name;
        ASSERT_TRUE(info->duration) << name;
        EXPECT_GT(*info->duration, wiola::units::Time{}) << name;

        auto metadata{(*asset)->read_metadata()};
        ASSERT_TRUE(metadata) << name;

        auto decoder{(*asset)->create_decoder()};
        ASSERT_TRUE(decoder) << name;
        std::array<float, 8> samples{};
        auto read{(*decoder)->render(samples)};
        ASSERT_TRUE(read) << name;
        EXPECT_GT(*read, 0U) << name;
    }
}

TEST(MediaProvider, CreatesIndependentDecodersForOneAsset)
{
    wiola::codec::LocalMediaProvider provider;
    auto asset{provider.open(fixture("tone.wav"))};
    ASSERT_TRUE(asset);

    auto first{(*asset)->create_decoder()};
    auto second{(*asset)->create_decoder()};
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);

    std::array<float, 8> samples{};
    ASSERT_GT((*first)->render(samples).value(), 0U);
    EXPECT_EQ((*first)->num_frames_left(), (*second)->num_frames_left() - wiola::pcm::Frames{4});
}

TEST(MediaProvider, RejectsAnUnavailableResourceBeforeCreatingAnAsset)
{
    wiola::codec::LocalMediaProvider provider;
    auto asset{provider.open("/nonexistent/wiola.wav")};

    ASSERT_FALSE(asset);
    EXPECT_EQ(asset.error().category, wiola::media::ErrorCategory::unreadable);
}

TEST(MediaProvider, ExposesItsConfiguredPlaybackCapabilities)
{
    const wiola::codec::MediaBackendRegistry registry;
    const wiola::codec::LocalMediaProvider provider{registry};

    EXPECT_EQ(provider.playback_extensions(), registry.playback_extensions());
}

} // namespace
