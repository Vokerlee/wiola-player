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

#include <app/search_index.hpp>

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

} // namespace
