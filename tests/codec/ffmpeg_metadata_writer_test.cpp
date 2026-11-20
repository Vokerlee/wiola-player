/**
 * @file
 * @brief Transactional metadata remux writer regression tests.
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

#include <codec/backends/ffmpeg/metadata.hpp>
#include <codec/backends/ffmpeg/metadata_writer.hpp>

#include <gtest/gtest.h>

extern "C" {
#include <libavutil/error.h>
}

#include <cerrno>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

using wiola::media::MetadataWriteError;
using wiola::media::MetadataWriteState;

std::filesystem::path fixture(const char* name)
{
    return std::filesystem::path{WIOLA_TEST_DATA_DIR} / name;
}

std::filesystem::path temporary(const char* name)
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove(path);

    return path;
}

struct RemoveOnExit {
    std::filesystem::path path;

    ~RemoveOnExit() { std::filesystem::remove(path); }
};

std::vector<char> bytes(const std::filesystem::path& path)
{
    std::ifstream input{path, std::ios::binary};

    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

wiola::media::MetadataWriteRequest title_request()
{
    wiola::media::MetadataWriteRequest request;
    request.overrides.title = "Changed";
    return request;
}

TEST(MetadataRemux, WritesRequestedFieldsWithoutChangingTheSource)
{
    const auto output = temporary("wiola-remux.mp3");
    RemoveOnExit cleanup{output};
    const auto original = bytes(fixture("tone.mp3"));

    wiola::media::MetadataWriteRequest request;
    request.overrides.title = "Remuxed";
    request.overrides.artist = "Artist";
    request.overrides.track_number = 7;

    const auto outcome = wiola::codec::ffmpeg::remux_metadata(fixture("tone.mp3"), output, request);

    ASSERT_EQ(outcome.state, MetadataWriteState::written) << outcome.detail;
    EXPECT_EQ(outcome.error, MetadataWriteError::none);
    EXPECT_FALSE(outcome.source_modified);
    EXPECT_EQ(bytes(fixture("tone.mp3")), original);

    const auto metadata = wiola::codec::ffmpeg::read_metadata(output);
    ASSERT_TRUE(metadata) << metadata.error().detail;
    EXPECT_EQ(metadata->track.title, "Remuxed");
    EXPECT_EQ(metadata->track.artist, "Artist");
    EXPECT_EQ(metadata->track.track_number, 7);
}

TEST(MetadataRemux, NeverOverwritesExistingOutputOrTheSource)
{
    const auto output = temporary("wiola-remux-existing.mp3");
    RemoveOnExit cleanup{output};
    {
        std::ofstream existing{output};
        existing << "keep";
    }

    wiola::media::MetadataWriteRequest request;
    request.overrides.title = "Changed";

    auto outcome = wiola::codec::ffmpeg::remux_metadata(fixture("tone.mp3"), output, request);
    EXPECT_EQ(outcome.state, MetadataWriteState::failed);
    EXPECT_EQ(outcome.error, MetadataWriteError::invalid_output);
    const std::vector<char> expected{'k', 'e', 'e', 'p'};
    EXPECT_EQ(bytes(output), expected);

    const auto original = bytes(fixture("tone.mp3"));
    outcome =
        wiola::codec::ffmpeg::remux_metadata(fixture("tone.mp3"), fixture("tone.mp3"), request);
    EXPECT_EQ(outcome.error, MetadataWriteError::invalid_edit);
    EXPECT_EQ(bytes(fixture("tone.mp3")), original);
}

TEST(MetadataRemux, EmptyAndCancelledRequestsLeaveNoPartialOutput)
{
    const auto output = temporary("wiola-remux-empty.mp3");
    RemoveOnExit cleanup{output};

    auto outcome = wiola::codec::ffmpeg::remux_metadata(fixture("tone.mp3"), output, {});
    EXPECT_EQ(outcome.state, MetadataWriteState::unchanged);
    EXPECT_FALSE(std::filesystem::exists(output));

    std::stop_source stop;
    stop.request_stop();
    wiola::media::OperationContext context{.stop = stop.get_token(), .deadline = {}};

    wiola::media::MetadataWriteRequest request;
    request.overrides.title = "Changed";
    outcome = wiola::codec::ffmpeg::remux_metadata(fixture("tone.mp3"), output, request, context);

    EXPECT_EQ(outcome.state, MetadataWriteState::failed);
    EXPECT_EQ(outcome.error, MetadataWriteError::cancelled);
    EXPECT_FALSE(std::filesystem::exists(output));
}

TEST(MetadataRemux, RejectsMalformedMp3WithoutLeavingOutput)
{
    const auto input = temporary("wiola-remux-malformed.mp3");
    const auto output = temporary("wiola-remux-malformed-output.mp3");
    RemoveOnExit cleanup_input{input};
    RemoveOnExit cleanup_output{output};
    {
        std::ofstream malformed{input, std::ios::binary};
        malformed << "not an MP3";
    }

    const auto original = bytes(input);
    wiola::media::MetadataWriteRequest request;
    request.overrides.title = "Changed";
    const auto outcome = wiola::codec::ffmpeg::remux_metadata(input, output, request);

    EXPECT_EQ(outcome.state, MetadataWriteState::failed);
    EXPECT_FALSE(outcome.source_modified);
    EXPECT_EQ(bytes(input), original);
    EXPECT_FALSE(std::filesystem::exists(output));
}

} // namespace

TEST(MetadataRemux, InterruptedAndDiskFullWritesRemovePartialOutput)
{
    const auto cancelled_output = temporary("wiola-remux-interrupted.mp3");
    const auto full_output = temporary("wiola-remux-disk-full.mp3");
    RemoveOnExit cleanup_cancelled{cancelled_output};
    RemoveOnExit cleanup_full{full_output};

    std::stop_source stop;
    wiola::media::OperationContext context{.stop = stop.get_token(), .deadline = {}};
    wiola::codec::ffmpeg::MetadataRemuxHooks interruption;
    interruption.after_packet = [&stop] {
        stop.request_stop();
        return 0;
    };

    auto outcome = wiola::codec::ffmpeg::remux_metadata(fixture("tone.mp3"), cancelled_output,
        title_request(), context, interruption);
    EXPECT_EQ(outcome.state, MetadataWriteState::failed);
    EXPECT_EQ(outcome.error, MetadataWriteError::cancelled);
    EXPECT_FALSE(std::filesystem::exists(cancelled_output));

    wiola::codec::ffmpeg::MetadataRemuxHooks disk_full;
    disk_full.after_packet = [] {
        return AVERROR(ENOSPC);
    };
    outcome = wiola::codec::ffmpeg::remux_metadata(fixture("tone.mp3"), full_output,
        title_request(), {}, disk_full);

    EXPECT_EQ(outcome.state, MetadataWriteState::failed);
    EXPECT_EQ(outcome.error, MetadataWriteError::io);
    EXPECT_FALSE(outcome.source_modified);
    EXPECT_FALSE(std::filesystem::exists(full_output));
}
