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

#include <app/library/local_identity.hpp>
#include <fixtures/temporary_directory.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace {

using wiola::app::LocalPathCase;
using wiola::app::media_id_for;
using wiola::testing::TemporaryDirectory;

TEST(MediaLocation, EmptyPathHasNoIdentity)
{
    EXPECT_FALSE(media_id_for({}).valid());
}

TEST(MediaLocation, NormalizesAbsolutePathComponentsWithoutReadingTheFile)
{
    const auto root = std::filesystem::current_path().root_path();
    const auto path = root / "music/album/../track.flac";
    const auto expected = root / "music/track.flac";

    EXPECT_EQ(media_id_for(path, LocalPathCase::sensitive).value, expected.generic_string());
}

TEST(MediaLocation, MakesARelativePathAbsoluteBeforeNormalizingIt)
{
    const auto expected = std::filesystem::current_path() / "track.flac";

    EXPECT_EQ(media_id_for("album/../track.flac", LocalPathCase::sensitive).value,
        expected.generic_string());
}

TEST(MediaLocation, SeparatesPathCaseFromIdentityCase)
{
    const auto path = std::filesystem::absolute("Music/Track.FLAC");
    const auto lower = std::filesystem::absolute("music/track.flac");

    EXPECT_EQ(media_id_for(path, LocalPathCase::sensitive).value, path.generic_string());
    EXPECT_EQ(media_id_for(path, LocalPathCase::insensitive),
        media_id_for(lower, LocalPathCase::insensitive));
    EXPECT_NE(media_id_for(path, LocalPathCase::sensitive),
        media_id_for(lower, LocalPathCase::sensitive));
}

TEST(MediaLocation, NativeCasePolicyMatchesTheHostFileConvention)
{
#ifdef _WIN32
    constexpr auto expected = LocalPathCase::insensitive;
#else
    constexpr auto expected = LocalPathCase::sensitive;
#endif
    EXPECT_EQ(media_id_for("Music/Track.FLAC"), media_id_for("Music/Track.FLAC", expected));
}

TEST(MediaLocation, FoldsTheAsciiAlphabetAndPreservesOtherPathBytes)
{
    const std::filesystem::path upper{u8"ABCDEFGHIJKLMNOPQRSTUVWXYZ-[0-9]-É_Я_音.flac"};
    const std::filesystem::path lower{u8"abcdefghijklmnopqrstuvwxyz-[0-9]-É_Я_音.flac"};
    const std::filesystem::path other{u8"abcdefghijklmnopqrstuvwxyz-[0-9]-é_я_音.flac"};

    EXPECT_EQ(media_id_for(upper, LocalPathCase::insensitive),
        media_id_for(lower, LocalPathCase::insensitive));
    EXPECT_NE(media_id_for(upper, LocalPathCase::insensitive),
        media_id_for(other, LocalPathCase::insensitive));
}

TEST(MediaLocation, DoesNotResolveASymlinkAlias)
{
    const TemporaryDirectory directory;
    const auto target = directory.path() / "track.wav";
    const auto alias = directory.path() / "alias.wav";

    std::ofstream{target} << "fixture";

    std::error_code error;
    std::filesystem::create_symlink(target.filename(), alias, error);

    if (error)
        GTEST_SKIP() << "The test filesystem cannot create a symlink: " << error.message();

    EXPECT_EQ(media_id_for(alias, LocalPathCase::sensitive).value, alias.generic_string());
    EXPECT_NE(media_id_for(alias), media_id_for(target));
}

} // namespace
