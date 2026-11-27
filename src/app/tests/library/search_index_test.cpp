/**
 * @file
 * @brief SearchIndex behavior and allocation-failure tests.
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
#include <app/library/search_index.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <new>
#include <string>
#include <vector>

namespace {

using wiola::testing::index_record;

TEST(SearchIndex, UpdatesIncrementallyWithoutDuplicatesAndMatchesStructuredQueries)
{
    wiola::app::SearchIndex index;
    const auto first = index_record("first", "Alpha", "Shared Artist", "Jazz");
    const auto second = index_record("second", "Beta", "Other Artist", "Rock");

    ASSERT_TRUE(index.upsert(first));
    ASSERT_TRUE(index.upsert(second));
    EXPECT_EQ(index.size(), 2);
    EXPECT_FALSE(index.upsert(first));
    EXPECT_EQ(index.size(), 2);

    wiola::library::SearchQuery query;
    query.artist = "shared artist";
    query.filters = {
        {.key = "genre", .value = "jazz", .mode = wiola::library::TagFilterMode::include}
    };
    EXPECT_EQ(index.query(query), std::vector<wiola::library::MediaId>{first.id});

    auto edited = first;
    edited.embedded.title = "Renamed";
    edited.overrides.artist = "Different Artist";
    ASSERT_TRUE(index.upsert(edited));
    EXPECT_TRUE(index.query(query).empty());

    query = {};
    query.title = "renamed";
    query.artist = "different";
    EXPECT_EQ(index.query(query), std::vector<wiola::library::MediaId>{first.id});

    EXPECT_TRUE(index.remove(first.id));
    EXPECT_FALSE(index.remove(first.id));
    EXPECT_EQ(index.query({}), std::vector<wiola::library::MediaId>{second.id});

    index.clear();
    EXPECT_EQ(index.size(), 0);
    EXPECT_FALSE(index.upsert(wiola::library::TrackRecord{}));
}

TEST(SearchIndex, MatchesDirectEvaluationOnFixedDataAndInvalidatesOnlySearchableChanges)
{
    wiola::app::SearchIndex index;
    std::vector<wiola::library::TrackRecord> records;

    for (int i = 0; i < 1000; ++i) {
        auto track = index_record("id-" + std::to_string(i), "Song " + std::to_string(i),
            i % 2 ? "Odd" : "Even", i % 3 ? "Rock" : "Jazz");

        if (i % 5 == 0)
            track.overrides.title = "Renamed";

        if (i % 7 == 0)
            track.overrides.artist = "";

        track.tags.push_back(
            {.id = {"extra"}, .key = "Mood", .value = i % 2 ? "Calm" : "Energetic"}
        );
        index.upsert(track);
        records.push_back(std::move(track));
    }

    const std::vector<wiola::library::SearchQuery> queries{
        {},
        {.title = " SONG ", .artist = "EVEN"},
        {.title = "renamed", .tags = "calm"},
        {.filters = {{"GENRE", "jazz"},
             {"mood", "energetic", wiola::library::TagFilterMode::exclude}}},
        {.tags = "MOOD"},
        {.album = "album", .filters = {{"genre", "rock"}}}
    };
    std::chrono::steady_clock::duration direct_time{}, indexed_time{};

    for (const auto& query : queries) {
        auto started = std::chrono::steady_clock::now();
        std::vector<wiola::library::MediaId> expected;

        for (const auto& track : records)
            if (wiola::library::matches_search(query, track))
                expected.push_back(track.id);

        std::ranges::sort(expected);
        direct_time += std::chrono::steady_clock::now() - started;
        started = std::chrono::steady_clock::now();
        EXPECT_EQ(index.query(query), expected);

        indexed_time += std::chrono::steady_clock::now() - started;
    }

    RecordProperty("direct_microseconds",
        std::chrono::duration_cast<std::chrono::microseconds>(direct_time).count());
    RecordProperty("indexed_microseconds",
        std::chrono::duration_cast<std::chrono::microseconds>(indexed_time).count());

    auto edited = records.front();
    edited.metadata_revision++;
    edited.tags.front().id = {"different-id"};
    EXPECT_FALSE(index.upsert(edited));

    edited.overrides.title = "Unique replacement";
    EXPECT_TRUE(index.upsert(edited));

    wiola::library::SearchQuery query;
    query.title = "unique";
    EXPECT_EQ(index.query(query), std::vector<wiola::library::MediaId>{edited.id});
}

TEST(SearchIndexBenchmark, ReusesNormalizedFieldsAcrossManyQueries)
{
    wiola::app::SearchIndex index;
    constexpr int tracks = 5000;
    constexpr int queries = 60;

    for (int value = 0; value < tracks; ++value) {
        index.upsert(index_record("media-" + std::to_string(value), "Song " + std::to_string(value),
            value % 2 == 0 ? "Even Artist" : "Odd Artist", value % 3 == 0 ? "Jazz" : "Rock"));
    }

    std::size_t matches = 0;
    const auto started = std::chrono::steady_clock::now();

    for (int iteration = 0; iteration < queries; ++iteration) {
        wiola::library::SearchQuery query;
        query.artist = iteration % 2 == 0 ? "even" : "odd";
        query.filters = {
            {.key = "genre", .value = "jazz", .mode = wiola::library::TagFilterMode::include}
        };
        matches += index.query(query).size();
    }

    const auto elapsed = std::chrono::steady_clock::now() - started;
    const double seconds = std::chrono::duration<double>{elapsed}.count();
    const auto rate = static_cast<int>(queries / seconds);

    RecordProperty("queries_per_second", rate);
    RecordProperty("matching_rows", matches);
    EXPECT_GT(matches, 0);
    EXPECT_GT(rate, 0);
}

TEST(SearchIndex, RevisionChangesOnlyWhenSearchableFactsChange)
{
    wiola::app::SearchIndex index;
    auto track = index_record("one", "Title", "Artist", "Genre");

    ASSERT_TRUE(index.upsert(track));

    const auto revision = index.revision();

    track.metadata_revision = 88;
    EXPECT_FALSE(index.upsert(track));
    EXPECT_EQ(index.revision(), revision);
    EXPECT_FALSE(index.remove({"missing"}));
    EXPECT_EQ(index.revision(), revision);

    track.embedded.title = "Changed";
    EXPECT_TRUE(index.upsert(track));
    EXPECT_GT(index.revision(), revision);

    const auto current = index.revision();

    index.clear();
    EXPECT_GT(index.revision(), current);

    const auto empty = index.revision();

    index.clear();
    EXPECT_EQ(index.revision(), empty);
}

TEST(SearchIndex, RepeatedNormalizedQueriesStayCorrectAcrossSearchableUpdates)
{
    wiola::app::SearchIndex index;
    auto track = index_record("media", "Song", "Artist", "Jazz");

    ASSERT_TRUE(index.upsert(track));

    const auto query = wiola::library::normalized_search_query({.title = " SONG "});
    const std::vector expected{track.id};

    EXPECT_EQ(index.query_normalized(query), expected);
    EXPECT_EQ(index.query_normalized(query), expected);

    track.overrides.title = "Different";
    ASSERT_TRUE(index.upsert(track));
    EXPECT_TRUE(index.query_normalized(query).empty());
    EXPECT_TRUE(index.query_normalized(query).empty());
}

TEST(SearchIndex, BorrowedFieldsSurviveUnrelatedUpdatesAndDisappearWithTheirEntry)
{
    wiola::app::SearchIndex index;
    const auto first = index_record("first", "Alpha", "Artist", "Jazz");
    auto second = index_record("second", "Beta", "Artist", "Rock");

    ASSERT_TRUE(index.upsert(first));
    ASSERT_TRUE(index.upsert(second));
    ASSERT_TRUE(index.contains(first.id));

    const auto* fields = index.fields(first.id);

    ASSERT_NE(fields, nullptr);
    EXPECT_EQ(fields->title, "alpha");

    second.overrides.title = "Changed";
    ASSERT_TRUE(index.upsert(second));
    EXPECT_EQ(fields->title, "alpha");
    EXPECT_EQ(index.fields(first.id), fields);
    ASSERT_TRUE(index.remove(first.id));
    EXPECT_FALSE(index.contains(first.id));
    EXPECT_EQ(index.fields(first.id), nullptr);

    index.clear();
    EXPECT_EQ(index.fields(second.id), nullptr);
}

#if WIOLA_TEST_ALLOCATOR_REPLACEMENT
TEST(SearchIndex, InterruptedRebuildCannotReuseAnOldQueryLabel)
{
    wiola::app::SearchIndex index;
    const std::string prefix(256, 'i');
    const auto needle = index_record(prefix + "needle", "Needle", "Artist", "Jazz");
    const auto other = index_record(prefix + "other", "Other", "Artist", "Rock");

    ASSERT_TRUE(index.upsert(needle));
    ASSERT_TRUE(index.upsert(other));

    const wiola::library::SearchQuery query{.title = "needle"};
    const std::vector expected{needle.id};

    ASSERT_EQ(index.query(query), expected);

    bool failed = false;
    {
        const wiola::testing::AllocationFailure failure{0};
        try {
            static_cast<void>(index.query_normalized({}));
        } catch (const std::bad_alloc&) {
            failed = true;
        }
    }
    ASSERT_TRUE(failed);
    EXPECT_EQ(index.query(query), expected);
}
#endif

} // namespace
