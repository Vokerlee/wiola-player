/**
 * @file
 * @brief Structured multi-field search regression tests.
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

#include <allocation_failure.hpp>
#include <library/search.hpp>
#include <new>

#include <gtest/gtest.h>

namespace {

using wiola::library::SearchQuery;

class Search : public testing::Test {
protected:
    Search()
    {
        track.id = wiola::library::MediaId{"media"};
        track.embedded.title = "Embedded title";
        track.embedded.artist = "Embedded artist";
        track.embedded.album = "Embedded album";
        track.overrides.title = "My Song";
        track.overrides.artist = "The Artist";
        track.overrides.album = "Great Album";
        track.tags = {
            {.id = wiola::library::TagId{"tag-1"}, .key = "Genre", .value = "Nu Jazz"},
            {.id = wiola::library::TagId{"tag-2"}, .key = "Mood",  .value = "Calm"   }
        };
    }

    wiola::library::TrackRecord track;
};

TEST_F(Search, NormalizesEveryQueryField)
{
    const SearchQuery normalized = wiola::library::normalized_search_query({.title = "  My Song ",
        .artist = " THE Artist ",
        .album = " great ALBUM ",
        .tags = " NU jazz "});

    EXPECT_EQ(normalized.title, "my song");
    EXPECT_EQ(normalized.artist, "the artist");
    EXPECT_EQ(normalized.album, "great album");
    EXPECT_EQ(normalized.tags, "nu jazz");
    EXPECT_TRUE(wiola::library::matches_search(normalized, track));
}

TEST_F(Search, RequiresEverySpecifiedFieldGroupToMatch)
{
    EXPECT_TRUE(wiola::library::matches_search(
        {.title = "song", .artist = "artist", .album = "great", .tags = "calm"}, track
    ));

    EXPECT_FALSE(wiola::library::matches_search(
        {.title = "song", .artist = "someone else", .album = {}, .tags = {}}, track
    ));
    EXPECT_FALSE(wiola::library::matches_search(
        {.title = {}, .artist = {}, .album = "missing", .tags = {}}, track
    ));
    EXPECT_TRUE(wiola::library::matches_search({}, track));
}

TEST_F(Search, MatchesTagKeysAndValuesWithNormalizedTerms)
{
    const auto with_tag = [](std::string value) {
        return SearchQuery{.title = {}, .artist = {}, .album = {}, .tags = std::move(value)};
    };

    EXPECT_TRUE(wiola::library::matches_search(with_tag("genre"), track));
    EXPECT_TRUE(wiola::library::matches_search(with_tag("JAZZ"), track));
    EXPECT_TRUE(wiola::library::matches_search(with_tag("mood"), track));
    EXPECT_FALSE(wiola::library::matches_search(with_tag("energetic"), track));
}

TEST_F(Search, AppliesIncludeAndExcludeFiltersWithNormalizedValues)
{
    SearchQuery query;
    query.filters = {
        {.key = " genre ", .value = " NU JAZZ ", .mode = wiola::library::TagFilterMode::include},
        {.key = "Mood",    .value = "Energetic", .mode = wiola::library::TagFilterMode::exclude}
    };

    EXPECT_TRUE(wiola::library::matches_search(query, track));

    query.filters.back().value = "Calm";
    EXPECT_FALSE(wiola::library::matches_search(query, track));

    query.filters = {
        {.key = "Mood", .value = {}, .mode = wiola::library::TagFilterMode::exclude}
    };
    EXPECT_FALSE(wiola::library::matches_search(query, track));

    query.filters = {
        {.key = "Missing", .value = {}, .mode = wiola::library::TagFilterMode::exclude}
    };
    EXPECT_TRUE(wiola::library::matches_search(query, track));
}

TEST(SearchFields, TitleFallbackAndExplicitEmptyLayersKeepTheirSearchMeaning)
{
    wiola::library::TrackRecord track;
    track.locations = {"missing/file-title.flac"};
    track.embedded.artist = "Embedded Artist";
    track.embedded.album = "Embedded Album";
    auto fields = wiola::library::search_fields_of(track);
    EXPECT_EQ(fields.title, "file-title");
    EXPECT_EQ(fields.artist, "embedded artist");
    EXPECT_EQ(fields.album, "embedded album");
    EXPECT_TRUE(wiola::library::matches_search({.title = "file-title"}, track));
    track.overrides.title = "";
    track.overrides.artist = "";
    track.overrides.album = "";
    fields = wiola::library::search_fields_of(track);
    EXPECT_TRUE(fields.title.empty());
    EXPECT_TRUE(fields.artist.empty());
    EXPECT_TRUE(fields.album.empty());
    EXPECT_FALSE(wiola::library::matches_search({.title = "file-title"}, track));
}

#if WIOLA_TEST_ALLOCATOR_REPLACEMENT
TEST(SearchFields, UnsearchedPayloadDoesNotAllocateSearchState)
{
    wiola::library::TrackRecord track;
    track.embedded.comment.assign(4096, 'c');
    track.embedded.composer.assign(4096, 'a');
    track.embedded.genre.assign(4096, 'g');
    wiola::library::SearchFields fields;
    bool failed = false;
    {
        const wiola::testing::AllocationFailure failure{0};
        try {
            fields = wiola::library::search_fields_of(track);
        } catch (const std::bad_alloc&) {
            failed = true;
        }
    }
    EXPECT_FALSE(failed);
    EXPECT_TRUE(fields.title.empty());
    EXPECT_TRUE(fields.artist.empty());
    EXPECT_TRUE(fields.album.empty());
    EXPECT_TRUE(fields.tags.empty());
}
#endif

} // namespace
