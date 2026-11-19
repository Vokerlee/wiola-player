/**
 * @file
 * @brief Absolute, lexical, case, and symlink policy tests for media identities.
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

#include <library/location.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <random>
#include <string>

namespace {

using wiola::library::media_id_for;
using wiola::library::MediaPathCase;

struct TemporaryDirectory {
    std::filesystem::path path{std::filesystem::temp_directory_path() /
        ("wiola-location-" + std::to_string(std::random_device{}()))};

    TemporaryDirectory() { std::filesystem::create_directories(path); }

    ~TemporaryDirectory() { std::filesystem::remove_all(path); }
};

TEST(MediaLocation, EmptyPathHasNoIdentity)
{
    EXPECT_FALSE(media_id_for({}).valid());
}

TEST(MediaLocation, NormalizesAbsolutePathComponentsWithoutReadingTheFile)
{
    const std::filesystem::path path{"/tmp/music/album/../track.flac"};

    EXPECT_EQ(media_id_for(path).value, "/tmp/music/track.flac");
}

TEST(MediaLocation, MakesARelativePathAbsoluteBeforeNormalizingIt)
{
    const std::filesystem::path expected{std::filesystem::current_path() / "track.flac"};

    EXPECT_EQ(media_id_for("album/../track.flac").value, expected.generic_string());
}

TEST(MediaLocation, SeparatesPathCaseFromIdentityCase)
{
    const std::filesystem::path path{"/Music/Track.FLAC"};

    EXPECT_EQ(media_id_for(path, MediaPathCase::sensitive).value, "/Music/Track.FLAC");
    EXPECT_EQ(media_id_for(path, MediaPathCase::insensitive).value, "/music/track.flac");
}

TEST(MediaLocation, DoesNotResolveASymlinkAlias)
{
    const TemporaryDirectory directory;
    const std::filesystem::path target{directory.path / "track.wav"};
    const std::filesystem::path alias{directory.path / "alias.wav"};

    std::ofstream{target} << "fixture";
    std::filesystem::create_symlink(target.filename(), alias);

    EXPECT_EQ(media_id_for(alias).value, alias.generic_string());
    EXPECT_NE(media_id_for(alias).value, media_id_for(target).value);
}

} // namespace
