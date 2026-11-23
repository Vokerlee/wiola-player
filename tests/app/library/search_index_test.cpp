/**
 * @file
 * @brief Incremental normalized search index tests and throughput benchmark.
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

#include <algorithm>
#include <app/library/location_index.hpp>
#include <app/library/search_index.hpp>
#include <app/library/tag_key_catalog.hpp>
#include <library/location.hpp>

#include <gtest/gtest.h>

#include <chrono>

namespace {

wiola::library::TrackRecord record(std::string id, std::string title, std::string artist,
    std::string genre)
{
    wiola::library::TrackRecord track;
    track.id = wiola::library::MediaId{std::move(id)};
    track.embedded.title = std::move(title);
    track.embedded.artist = std::move(artist);
    track.embedded.album = "Album";
    track.tags = {
        {.id = wiola::library::TagId{"tag-" + track.id.value},
         .key = "Genre",
         .value = std::move(genre)}
    };

    return track;
}

TEST(SearchIndex, UpdatesIncrementallyWithoutDuplicatesAndMatchesStructuredQueries)
{
    wiola::app::SearchIndex index;
    const auto first = record("first", "Alpha", "Shared Artist", "Jazz");
    const auto second = record("second", "Beta", "Other Artist", "Rock");
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

TEST(TagKeyCatalog, CountsMediaAndRetainsLabelsAcrossValueAndCaseChanges)
{
    wiola::app::TagKeyCatalog catalog;
    auto first = record("one", "Song", "Artist", "Jazz");
    first.tags.push_back(first.tags.front());
    catalog.upsert(first);
    auto second = record("two", "Song", "Artist", "Rock");
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

TEST(SearchIndex, MatchesDirectEvaluationOnFixedDataAndInvalidatesOnlySearchableChanges)
{
    wiola::app::SearchIndex index;
    std::vector<wiola::library::TrackRecord> records;
    for (int i = 0; i < 1000; ++i) {
        auto track = record("id-" + std::to_string(i), "Song " + std::to_string(i),
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

TEST(LocationIndex, ResolvesAliasesWithoutFileReadsAndRemovesOldLocations)
{
    wiola::app::LocationIndex index;
    auto track = record("opaque", "Song", "Artist", "Jazz");
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

TEST(SearchIndexBenchmark, ReusesNormalizedFieldsAcrossManyQueries)
{
    wiola::app::SearchIndex index;
    constexpr int tracks = 5000;
    constexpr int queries = 60;

    for (int value = 0; value < tracks; ++value) {
        index.upsert(record("media-" + std::to_string(value), "Song " + std::to_string(value),
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

TEST(TagKeyCatalog, RevisionChangesForGlobalKeysAndNotForValueOrOccurrenceChanges)
{
    using namespace wiola;
    app::TagKeyCatalog catalog;
    auto first = record("one", "Title", "Artist", "Genre");
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

TEST(SearchIndex, RevisionChangesOnlyWhenSearchableFactsChange)
{
    wiola::app::SearchIndex index;
    auto track = record("one", "Title", "Artist", "Genre");
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

} // namespace
