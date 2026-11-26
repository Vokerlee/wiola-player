/**
 * @file
 * @brief Contract tests for the format-neutral local media facade.
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

#include <media/codecs/ffmpeg/composition.hpp>
#include <media/local/provider.hpp>

#include <media/local/resource.hpp>

#include <fixtures/temporary_directory.hpp>

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stop_token>

namespace {

std::filesystem::path fixture(const char* name)
{
    return std::filesystem::path{WIOLA_TEST_DATA_DIR} / name;
}

TEST(MediaProvider, OpensEverySupportedFormatThroughOneFacade)
{
    wiola::codec::LocalMediaProvider provider{wiola::codec::ffmpeg::make_registry()};

    for (const char* name : {"tone.wav", "tone.flac", "tone.mp3"}) {
        auto asset{provider.open(wiola::media::local_resource(fixture(name)))};
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

TEST(MediaProvider, DistinguishesCancellationFromDeadlineExpiry)
{
    wiola::codec::LocalMediaProvider provider{wiola::codec::ffmpeg::make_registry()};
    std::stop_source stop;
    stop.request_stop();

    const wiola::media::OperationContext cancelled_context{.stop = stop.get_token(),
        .deadline = {}};
    auto cancelled{
        provider.open(wiola::media::local_resource("not-a-real-file.wav"), cancelled_context)};
    ASSERT_FALSE(cancelled);
    EXPECT_EQ(cancelled.error().category, wiola::media::ErrorCategory::cancelled);

    const wiola::media::OperationContext expired{.stop = {},
        .deadline = wiola::media::OperationContext::Clock::now() - std::chrono::seconds{1}};
    auto timed_out{provider.open(wiola::media::local_resource("not-a-real-file.wav"), expired)};
    ASSERT_FALSE(timed_out);
    EXPECT_EQ(timed_out.error().category, wiola::media::ErrorCategory::timed_out);
}

TEST(MediaProvider, StopsAnOpenedDecoderAtItsNextWorkBoundary)
{
    wiola::codec::LocalMediaProvider provider{wiola::codec::ffmpeg::make_registry()};
    std::stop_source stop;
    const wiola::media::OperationContext context{.stop = stop.get_token(), .deadline = {}};
    auto asset{provider.open(wiola::media::local_resource(fixture("tone.wav")), context)};
    ASSERT_TRUE(asset);
    auto decoder{(*asset)->create_decoder(context)};
    ASSERT_TRUE(decoder);

    stop.request_stop();
    std::array<float, 8> samples{};
    auto read{(*decoder)->render(samples)};

    ASSERT_FALSE(read);
    EXPECT_EQ(read.error().category, wiola::media::ErrorCategory::cancelled);
}

TEST(MediaProvider, CreatesIndependentDecodersForOneAsset)
{
    wiola::codec::LocalMediaProvider provider{wiola::codec::ffmpeg::make_registry()};
    auto asset{provider.open(wiola::media::local_resource(fixture("tone.wav")))};
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
    wiola::testing::TemporaryDirectory directory;
    wiola::codec::LocalMediaProvider provider{wiola::codec::ffmpeg::make_registry()};
    auto asset{provider.open(wiola::media::local_resource(directory.path() / "wiola.wav"))};

    ASSERT_FALSE(asset);
    EXPECT_EQ(asset.error().category, wiola::media::ErrorCategory::unreadable);
}

TEST(MediaProvider, PropagatesInspectionFailureFromTheDecoderRegistry)
{
    wiola::testing::TemporaryDirectory directory;
    const std::filesystem::path path{directory.path() / "wiola_provider_not_audio.bin"};
    std::ofstream{path} << "not audio";

    wiola::codec::LocalMediaProvider provider{wiola::codec::ffmpeg::make_registry()};
    auto asset{provider.open(wiola::media::local_resource(path))};
    ASSERT_TRUE(asset);

    auto info{(*asset)->inspect()};

    ASSERT_FALSE(info);
    EXPECT_EQ(info.error().category, wiola::media::ErrorCategory::unsupported);
}

TEST(MediaProvider, ExposesItsConfiguredPlaybackCapabilities)
{
    const wiola::codec::MediaBackendRegistry registry{wiola::codec::ffmpeg::make_registry()};
    const wiola::codec::LocalMediaProvider provider{registry};

    EXPECT_EQ(provider.playback_extensions(), registry.playback_extensions());
}

TEST(MediaProvider, NeverTreatsAnotherSourcesKeyAsALocalFile)
{
    wiola::codec::LocalMediaProvider provider{wiola::codec::ffmpeg::make_registry()};
    const auto local{wiola::media::local_resource(fixture("tone.wav"))};
    const wiola::media::ResourceReference other{"different driver", local.key(), local.label()};
    const auto result{provider.open(other)};
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().category, wiola::media::ErrorCategory::unsupported);

    std::stop_source stop;
    stop.request_stop();
    const auto cancelled{provider.open(other, {.stop = stop.get_token(), .deadline = {}})};
    ASSERT_FALSE(cancelled);
    EXPECT_EQ(cancelled.error().category, wiola::media::ErrorCategory::cancelled);
    const auto expired{provider.open(other,
        {.stop = {},
            .deadline = wiola::media::OperationContext::Clock::now() - std::chrono::seconds{1}})};
    ASSERT_FALSE(expired);
    EXPECT_EQ(expired.error().category, wiola::media::ErrorCategory::timed_out);
}

} // namespace
