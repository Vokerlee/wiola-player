/**
 * @file
 * @brief Validated atomic media replacement regression tests.
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

#include <app/media_replacement.hpp>

#include <codec/backends/ffmpeg/metadata.hpp>
#include <codec/backends/ffmpeg/metadata_writer.hpp>
#include <codec/provider.hpp>
#include <library/fingerprint.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

namespace {

std::filesystem::path temporary(const char* name)
{
    const auto path = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove(path);

    return path;
}

struct Cleanup {
    std::vector<std::filesystem::path> paths;

    ~Cleanup()
    {
        for (const auto& path : paths)
            std::filesystem::remove(path);
    }
};

std::vector<char> bytes(const std::filesystem::path& path)
{
    std::ifstream input{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

wiola::media::MetadataWriteRequest title_request()
{
    wiola::media::MetadataWriteRequest request;
    request.overrides.title = "Replaced title";
    return request;
}

TEST(MediaReplacement, ValidatesAndCommitsWhileRetainingTheOriginal)
{
    const auto source = temporary("wiola-replacement.mp3");
    const auto output = temporary("wiola-replacement-output.mp3");
    auto backup = source;
    backup += ".wiola-backup";
    Cleanup cleanup{
        {source, output, backup}
    };

    std::filesystem::copy_file(std::filesystem::path{WIOLA_TEST_DATA_DIR} / "tone.mp3", source);
    const auto original = bytes(source);
    const auto expected = wiola::library::fingerprint_media(source);
    ASSERT_TRUE(expected);

    ASSERT_EQ(wiola::codec::ffmpeg::remux_metadata(source, output, title_request()).state,
        wiola::media::MetadataWriteState::written);

    wiola::codec::LocalMediaProvider provider;
    const auto outcome = wiola::app::commit_media_replacement(source, output, provider, *expected);

    ASSERT_EQ(outcome.state, wiola::media::MetadataWriteState::written) << outcome.detail;
    EXPECT_TRUE(outcome.source_modified);
    EXPECT_FALSE(std::filesystem::exists(output));
    EXPECT_EQ(bytes(backup), original);

    const auto metadata = wiola::codec::ffmpeg::read_metadata(source);
    ASSERT_TRUE(metadata);
    EXPECT_EQ(metadata->track.title, "Replaced title");
}

TEST(MediaReplacement, RejectsInvalidOutputWithoutChangingTheSource)
{
    const auto source = temporary("wiola-invalid-replacement.mp3");
    const auto output = temporary("wiola-invalid-replacement-output.mp3");
    auto backup = source;
    backup += ".wiola-backup";
    Cleanup cleanup{
        {source, output, backup}
    };

    std::filesystem::copy_file(std::filesystem::path{WIOLA_TEST_DATA_DIR} / "tone.mp3", source);
    const auto original = bytes(source);
    {
        std::ofstream invalid{output, std::ios::binary};
        invalid << "invalid media";
    }

    wiola::codec::LocalMediaProvider provider;
    const auto outcome = wiola::app::commit_media_replacement(source, output, provider);

    EXPECT_EQ(outcome.state, wiola::media::MetadataWriteState::failed);
    EXPECT_EQ(outcome.error, wiola::media::MetadataWriteError::invalid_output);
    EXPECT_FALSE(outcome.source_modified);
    EXPECT_EQ(bytes(source), original);
    EXPECT_FALSE(std::filesystem::exists(output));
    EXPECT_FALSE(std::filesystem::exists(backup));
}

TEST(MediaReplacement, RejectsAnExternalEditBeforeCommit)
{
    const auto source = temporary("wiola-conflicted-replacement.mp3");
    const auto output = temporary("wiola-conflicted-replacement-output.mp3");
    auto backup = source;
    backup += ".wiola-backup";
    Cleanup cleanup{
        {source, output, backup}
    };

    std::filesystem::copy_file(std::filesystem::path{WIOLA_TEST_DATA_DIR} / "tone.mp3", source);
    const auto expected = wiola::library::fingerprint_media(source);
    ASSERT_TRUE(expected);
    ASSERT_EQ(wiola::codec::ffmpeg::remux_metadata(source, output, title_request()).state,
        wiola::media::MetadataWriteState::written);

    {
        std::ofstream changed{source, std::ios::app};
        changed << "external edit";
    }
    const auto changed = bytes(source);

    wiola::codec::LocalMediaProvider provider;
    const auto outcome = wiola::app::commit_media_replacement(source, output, provider, *expected);

    EXPECT_EQ(outcome.state, wiola::media::MetadataWriteState::failed);
    EXPECT_EQ(outcome.error, wiola::media::MetadataWriteError::conflicted);
    EXPECT_FALSE(outcome.source_modified);
    EXPECT_EQ(bytes(source), changed);
    EXPECT_FALSE(std::filesystem::exists(backup));
}

} // namespace
