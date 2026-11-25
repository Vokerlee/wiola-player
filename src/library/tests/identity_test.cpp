/**
 * @file
 * @brief Type-safety and persistence-value tests for library identities.
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

#include <wiola/library/identity.hpp>

#include <gtest/gtest.h>

#include <type_traits>
#include <unordered_map>

namespace {

using namespace wiola::library;

static_assert(!std::is_convertible_v<MediaId, PlaylistId>);
static_assert(!std::is_convertible_v<PlaylistId, PlaylistEntryId>);
static_assert(!std::is_convertible_v<PlaylistEntryId, WorkspaceId>);
static_assert(!std::is_convertible_v<WorkspaceId, TagId>);
static_assert(!std::is_convertible_v<TagId, MediaId>);

TEST(Identity, StartsWithoutAPersistedValue)
{
    EXPECT_FALSE(MediaId{}.valid());
    EXPECT_FALSE(PlaylistEntryId{}.valid());
    EXPECT_FALSE(PlaylistId{}.valid());
    EXPECT_FALSE(WorkspaceId{}.valid());
    EXPECT_FALSE(TagId{}.valid());
}

TEST(Identity, KeepsTheValueThatIdentifiesTheRecord)
{
    const MediaId media{.value = "/music/track.flac"};
    const MediaId same{.value = "/music/track.flac"};
    const MediaId different{.value = "/music/other.flac"};

    EXPECT_TRUE(media.valid());
    EXPECT_EQ(media, same);
    EXPECT_NE(media, different);
    EXPECT_GT(media, different);
}

TEST(Identity, UsesTheWholeValueAsAnOpaqueHashKey)
{
    std::unordered_map<PlaylistEntryId, int> entries;
    const PlaylistEntryId entry{.value = "entry-01"};

    entries.emplace(entry, 42);

    EXPECT_EQ(entries.at(PlaylistEntryId{.value = "entry-01"}), 42);
    EXPECT_EQ(entries.count(PlaylistEntryId{.value = "entry-02"}), 0U);
}

} // namespace
