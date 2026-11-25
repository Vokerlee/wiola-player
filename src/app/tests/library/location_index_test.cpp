/**
 * @file
 * @brief LocationIndex behavior and allocation-failure tests.
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

#include "index_record.hpp"

#include <allocation_failure.hpp>
#include <app/library/location_index.hpp>
#include <gtest/gtest.h>

#include <new>
#include <string>
#include <vector>

namespace {

using wiola::testing::index_record;

TEST(LocationIndex, ResolvesAliasesWithoutFileReadsAndRemovesOldLocations)
{
    wiola::app::LocationIndex index;
    auto track = index_record("opaque", "Song", "Artist", "Jazz");
    track.locations = {"missing/../alias.wav", "second.wav"};
    index.upsert(track);
    EXPECT_EQ(index.find("alias.wav"), track.id);
    track.locations = {"moved.wav"};
    index.upsert(track);
    EXPECT_FALSE(index.find("alias.wav"));
    EXPECT_EQ(index.find("moved.wav"), track.id);
    index.remove(track.id);
    EXPECT_FALSE(index.find("moved.wav"));
}

TEST(LocationIndex, OverlappingAndNoopUpdatesRetainOtherOwnersAndIgnoreEmptyLocations)
{
    wiola::app::LocationIndex index;
    auto original = index_record("media-original", "Song", "Artist", "Jazz");
    original.locations = {"", "one.wav", "shared.wav"};
    auto other = index_record("zz-other", "Song", "Artist", "Rock");
    other.locations = {"shared.wav"};
    index.upsert(original);
    index.upsert(other);
    index.upsert(original);
    index.upsert({});
    index.remove({"missing"});
    EXPECT_EQ(index.find("one.wav"), original.id);
    EXPECT_EQ(index.find("shared.wav"), original.id);
    original.locations = {"one.wav", "two.wav"};
    index.upsert(original);
    index.upsert(original);
    EXPECT_EQ(index.find("shared.wav"), other.id);
    EXPECT_EQ(index.find("one.wav"), original.id);
    EXPECT_EQ(index.find("two.wav"), original.id);
    index.remove(original.id);
    index.remove(original.id);
    EXPECT_FALSE(index.find("one.wav"));
    EXPECT_FALSE(index.find("two.wav"));
    EXPECT_EQ(index.find("shared.wav"), other.id);
}

#if WIOLA_TEST_ALLOCATOR_REPLACEMENT
TEST(LocationIndex, EveryUpdateAllocationFailureRetainsBindingsAndSharedOwnership)
{
    bool observed_failure = false;
    for (std::size_t after = 0;; ++after) {
        wiola::app::LocationIndex index;
        auto original = index_record("media-original", "Song", "Artist", "Jazz");
        original.locations = {"shared-old.wav", "old-only.wav"};
        auto other = index_record("zz-other", "Song", "Artist", "Rock");
        other.locations = {"shared-old.wav"};
        index.upsert(original);
        index.upsert(other);
        auto updated = original;
        updated.locations = {"shared-old.wav", "new-location.wav"};
        bool failed = false;
        {
            const wiola::testing::AllocationFailure failure{after};
            try {
                index.upsert(updated);
            } catch (const std::bad_alloc&) {
                failed = true;
            }
        }
        EXPECT_EQ(index.find("shared-old.wav"), original.id);
        if (failed) {
            EXPECT_EQ(index.find("old-only.wav"), original.id);
            EXPECT_FALSE(index.find("new-location.wav"));
        } else {
            EXPECT_FALSE(index.find("old-only.wav"));
            EXPECT_EQ(index.find("new-location.wav"), original.id);
        }
        index.remove(original.id);
        EXPECT_EQ(index.find("shared-old.wav"), other.id);
        EXPECT_FALSE(index.find("old-only.wav"));
        EXPECT_FALSE(index.find("new-location.wav"));
        index.remove(other.id);
        EXPECT_FALSE(index.find("shared-old.wav"));
        if (!failed) {
            EXPECT_TRUE(observed_failure);
            break;
        }
        observed_failure = true;
    }
}
#endif

} // namespace
