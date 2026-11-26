/**
 * @file
 * @brief Native path conversion and local source namespace contracts.
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

#include <wiola/media/local/resource.hpp>

#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <string>

namespace {

using namespace wiola::media;

TEST(LocalResource, RoundTripsUnicodeWithoutRequiringAnExistingFile)
{
    const std::filesystem::path path{u8"missing/Музыка/曲.flac"};
    const auto resource{local_resource(path)};
    const auto restored{local_path(resource)};
    ASSERT_TRUE(restored);
    EXPECT_EQ(*restored, path);
    const std::u8string label{u8"曲"};
    EXPECT_EQ(resource.label(),
        std::string(reinterpret_cast<const char*>(label.data()), label.size()));
}

TEST(LocalResource, PreservesComponentIdentityWithoutCanonicalizingAliases)
{
    const std::filesystem::path repeated{"folder//song.wav"};
    const std::filesystem::path ordinary{"folder/song.wav"};
    ASSERT_EQ(repeated, ordinary);
    EXPECT_EQ(local_resource(repeated), local_resource(ordinary));
    EXPECT_NE(local_resource("./song.wav"), local_resource("song.wav"));
    EXPECT_NE(local_resource("folder/../song.wav"), local_resource("song.wav"));
    EXPECT_NE(local_resource("folder/"), local_resource("folder"));
}

TEST(LocalResource, RejectsAnotherProvidersKeyEvenWhenItNamesALocalFile)
{
    const ResourceReference other{"different driver", "song.wav", "Song"};
    const auto result{local_path(other)};
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().category, ErrorCategory::unsupported);
}

TEST(LocalResource, RejectsEmbeddedNullInsteadOfOpeningATruncatedPath)
{
    const auto local{local_resource("song.wav")};
    const ResourceReference malformed{
        local.source(), std::string{"song.wav\0another", 16},
         "Song"
    };
    const auto result{local_path(malformed)};
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().category, ErrorCategory::unreadable);
}

TEST(LocalResource, ConvertsBatchesInOrderWithDuplicateOccurrencesPreserved)
{
    const std::array paths{std::filesystem::path{"first.wav"}, std::filesystem::path{"first.wav"},
        std::filesystem::path{"second.flac"}};
    const auto resources{local_resources(paths)};
    ASSERT_EQ(resources.size(), paths.size());
    for (std::size_t i{}; i < paths.size(); ++i) {
        const auto restored{local_path(resources[i])};
        ASSERT_TRUE(restored);
        EXPECT_EQ(*restored, paths[i]);
    }
    EXPECT_EQ(resources[0], resources[1]);
    EXPECT_TRUE(local_resources({}).empty());
}

} // namespace
