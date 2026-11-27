/**
 * @file
 * @brief TagKeyCatalog behavior and allocation-failure tests.
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
#include <app/library/tag_key_catalog.hpp>

#include <gtest/gtest.h>

#include <new>
#include <string>
#include <vector>

namespace {

using wiola::testing::index_record;

TEST(TagKeyCatalog, CountsMediaAndRetainsLabelsAcrossValueAndCaseChanges)
{
    wiola::app::TagKeyCatalog catalog;
    auto first = index_record("one", "Song", "Artist", "Jazz");
    first.tags.push_back(first.tags.front());
    catalog.upsert(first);

    auto second = index_record("two", "Song", "Artist", "Rock");
    second.tags.front().key = "genre";
    catalog.upsert(second);
    EXPECT_EQ(catalog.keys(), std::vector<std::string>{"Genre"});

    catalog.remove(first.id);
    EXPECT_EQ(catalog.keys(), std::vector<std::string>{"Genre"});

    second.tags.front().key = "Mood";
    catalog.upsert(second);
    EXPECT_EQ(catalog.keys(), std::vector<std::string>{"Mood"});

    catalog.remove(second.id);
    EXPECT_TRUE(catalog.keys().empty());
}

TEST(TagKeyCatalog, RevisionChangesForGlobalKeysAndNotForValueOrOccurrenceChanges)
{
    using namespace wiola;

    app::TagKeyCatalog catalog;
    auto first = index_record("one", "Title", "Artist", "Genre");
    first.tags = {
        {{"tag"}, "Mood", "Calm"}
    };
    catalog.upsert(first);

    const auto revision = catalog.revision();

    first.tags[0].value = "Loud";
    catalog.upsert(first);
    EXPECT_EQ(catalog.revision(), revision);

    auto second = first;
    second.id = {"two"};
    catalog.upsert(second);
    EXPECT_EQ(catalog.revision(), revision);

    catalog.remove(first.id);
    EXPECT_EQ(catalog.revision(), revision);

    catalog.remove(second.id);
    EXPECT_GT(catalog.revision(), revision);
    EXPECT_TRUE(catalog.keys().empty());
}

TEST(TagKeyCatalog, OverlappingKeysPreserveLabelsAndCountOnlyGlobalTransitions)
{
    wiola::app::TagKeyCatalog catalog;
    auto original = index_record("media-original", "Song", "Artist", "Jazz");
    original.tags.push_back({.id = {"mood"}, .key = "Mood", .value = "Calm"});

    const auto other = index_record("other", "Song", "Artist", "Rock");

    catalog.upsert(original);
    catalog.upsert(other);

    const auto revision = catalog.revision();

    original.tags = {
        {.id = {"genre"},    .key = "genre",    .value = "Classical"      },
        {.id = {"composer"}, .key = "Composer", .value = "Artist"         },
        {.id = {"ignored"},  .key = " \t",      .value = "not a valid key"}
    };
    catalog.upsert(original);
    EXPECT_EQ(catalog.keys(), (std::vector<std::string>{"Composer", "Genre"}));
    EXPECT_EQ(catalog.revision(), revision + 2);

    catalog.upsert({});
    catalog.remove({"missing"});
    catalog.upsert(original);
    EXPECT_EQ(catalog.revision(), revision + 2);

    catalog.remove(original.id);
    EXPECT_EQ(catalog.keys(), (std::vector<std::string>{"Genre"}));

    catalog.remove(other.id);
    EXPECT_TRUE(catalog.keys().empty());
}

#if WIOLA_TEST_ALLOCATOR_REPLACEMENT
TEST(TagKeyCatalog, EveryUpdateAllocationFailureRetainsLabelsCountsAndRevision)
{
    bool observed_failure = false;
    for (std::size_t after = 0;; ++after) {
        wiola::app::TagKeyCatalog catalog;
        auto original = index_record("media-original", "Song", "Artist", "Jazz");
        original.tags.push_back({.id = {"mood"}, .key = "Mood", .value = "Calm"});

        const auto other = index_record("other", "Song", "Artist", "Rock");

        catalog.upsert(original);
        catalog.upsert(other);

        const auto labels = catalog.keys();
        const auto revision = catalog.revision();
        auto updated = original;
        updated.tags = {
            {.id = {"composer"}, .key = "Composer", .value = "Artist"}
        };

        bool failed = false;
        {
            const wiola::testing::AllocationFailure failure{after};
            try {
                catalog.upsert(updated);
            } catch (const std::bad_alloc&) {
                failed = true;
            }
        }

        if (failed) {
            EXPECT_EQ(catalog.keys(), labels);
            EXPECT_EQ(catalog.revision(), revision);
        } else {
            EXPECT_EQ(catalog.keys(), (std::vector<std::string>{"Composer", "Genre"}));
            EXPECT_EQ(catalog.revision(), revision + 2);
        }

        catalog.remove(original.id);
        EXPECT_EQ(catalog.keys(), (std::vector<std::string>{"Genre"}));

        catalog.remove(other.id);
        EXPECT_TRUE(catalog.keys().empty());

        if (!failed) {
            EXPECT_TRUE(observed_failure);
            break;
        }

        observed_failure = true;
    }
}
#endif

} // namespace
