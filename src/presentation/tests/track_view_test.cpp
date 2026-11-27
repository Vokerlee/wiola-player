/**
 * @file
 * @brief Metadata ownership, normalized queries and portable filtering contracts.
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

#include <app/library/search_index.hpp>
#include <wiola/library/in_memory_repository.hpp>
#include <wiola/presentation/tracks/track_commands.hpp>
#include <wiola/presentation/tracks/track_view_pipeline.hpp>
#include <wiola/presentation/tracks/track_view_state.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <map>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace wiola;

library::TrackRecord record(std::string id, std::string title)
{
    library::TrackRecord result;
    result.id = library::MediaId{std::move(id)};
    result.embedded.title = std::move(title);
    result.locations = {"music/track.flac"};
    return result;
}

TEST(TrackPresentation, OwnsResolvedMetadataAndPreservesOccurrenceIdentity)
{
    auto source = record("media", "Embedded");
    source.embedded = {.title = "Embedded",
        .artist = "Artist",
        .album = "Album",
        .duration = units::Time{42},
        .genre = "Jazz",
        .year = 2026,
        .comment = "Comment",
        .composer = "Composer",
        .track_number = 3,
        .disc_number = 2};
    source.overrides.title = "Override";
    source.tags = {
        {library::TagId{"tag"}, "Mood", "Calm"}
    };
    source.metadata_revision = 7;
    source.content_revision = 9;
    const library::PlaylistEntryId occurrence{"occurrence"};

    const auto row = gui::present_record(occurrence, source);
    auto expected = source.embedded;
    expected.title = "Override";

    EXPECT_EQ(row.id, gui::TrackRowId{occurrence});
    EXPECT_EQ(row.media, source.id);
    EXPECT_EQ(row.metadata, expected);
    EXPECT_EQ(row.tags, source.tags);
    EXPECT_EQ(row.location, source.locations.front());
    EXPECT_FALSE(row.location_key.empty());
    EXPECT_EQ(row.metadata_revision, 7);
    EXPECT_EQ(row.content_revision, 9);
    EXPECT_FALSE(row.missing);

    source.overrides.title = "Later";
    source.tags.clear();
    source.locations.clear();

    EXPECT_EQ(row.metadata.title, "Override");
    EXPECT_EQ(row.tags.size(), 1);
    EXPECT_EQ(row.location, "music/track.flac");
}

TEST(TrackPresentation, DistinguishesMissingTitleFromExplicitlyEmptyOverride)
{
    auto source = record("media", "");
    source.locations = {std::filesystem::path{u8"music/Песня.flac"}};
    const auto fallback = gui::present_record(engine::QueueEntryId{1}, source);
    const auto title = source.locations.front().stem().u8string();
    EXPECT_EQ(fallback.metadata.title, std::string(title.begin(), title.end()));

    source.overrides.title = "";
    EXPECT_TRUE(gui::present_record(engine::QueueEntryId{1}, source).metadata.title.empty());

    source.missing = true;
    EXPECT_TRUE(gui::present_record(engine::QueueEntryId{1}, source).missing);

    source.missing = false;
    source.locations.clear();
    source.overrides.title.reset();
    const auto unlocated = gui::present_record(engine::QueueEntryId{1}, source);

    EXPECT_TRUE(unlocated.missing);
    EXPECT_TRUE(unlocated.metadata.title.empty());
    EXPECT_TRUE(unlocated.location.empty());
}

TEST(TrackViewState, NormalizesAndDeduplicatesWithoutChangingFirstSortPriority)
{
    gui::TrackViewState state;
    state.query = {
        .title = "  TITLE ",
        .artist = " Artist ",
        .album = " ALBUM ",
        .tags = " Calm ",
        .filters = {{" Mood ", " CALM "}, {"mood", "calm"},
                    {"mood", "", library::TagFilterMode::exclude}, {"", "invalid"},
                    {"valid", std::string(1, '\x01')}}
    };
    state.sort_keys = {
        {.field = gui::TrackSortField::title, .tag_key = {}},
        {.field = gui::TrackSortField::title,
         .direction = gui::TrackSortDirection::descending,
         .tag_key = {}},
        gui::TrackSortKey::for_tag(" Mood "), gui::TrackSortKey::for_tag("mood"),
        gui::TrackSortKey::for_tag(""),
        {.field = static_cast<gui::TrackSortField>(999), .tag_key = {}}
    };

    const auto normalized = gui::normalized_track_view_state(state);

    EXPECT_EQ(normalized.query.title, "title");
    EXPECT_EQ(normalized.query.artist, "artist");
    EXPECT_EQ(normalized.query.album, "album");
    EXPECT_EQ(normalized.query.tags, "calm");
    EXPECT_EQ(normalized.query.filters,
        (std::vector<library::TagFilter>{
            {"mood", "calm"},
            {"mood", "", library::TagFilterMode::exclude}
    }));
    ASSERT_EQ(normalized.sort_keys.size(), 2);
    EXPECT_EQ(normalized.sort_keys[0].field, gui::TrackSortField::title);
    EXPECT_EQ(normalized.sort_keys[0].direction, gui::TrackSortDirection::ascending);
    EXPECT_EQ(normalized.sort_keys[1].tag_key, "mood");
    EXPECT_EQ(gui::normalized_track_view_state(normalized), normalized);
}

TEST(TrackViewPipeline, RecognizesEverySearchFieldAndPreservesUnfilteredOccurrences)
{
    EXPECT_FALSE(gui::track_query_active({}));

    for (const auto field : {&library::SearchQuery::title, &library::SearchQuery::artist,
             &library::SearchQuery::album, &library::SearchQuery::tags}) {
        library::SearchQuery query;
        query.*field = "term";
        EXPECT_TRUE(gui::track_query_active(query));
    }

    library::SearchQuery tagged;
    tagged.filters = {
        {"mood", "calm"}
    };
    EXPECT_TRUE(gui::track_query_active(tagged));

    auto first = gui::present_record(library::PlaylistEntryId{"first"}, record("same", "Song"));
    auto second = first;
    second.id = library::PlaylistEntryId{"second"};
    const std::vector source{first, second};

    EXPECT_EQ(gui::apply_track_view(source, nullptr, {}, {}), source);
    EXPECT_TRUE(gui::apply_track_view({}, nullptr, {}, {}).empty());
}

TEST(TrackViewPipeline, CombinesFieldsAndTagFiltersUsingRowsWithoutRepository)
{
    auto source = record("media", "Evening Song");
    source.embedded.artist = "Artist";
    source.embedded.album = "Album";
    source.tags = {
        {library::TagId{"mood"}, "Mood", "Calm"}
    };
    const auto row = gui::present_record(engine::QueueEntryId{1}, source);
    auto query = library::normalized_search_query({.title = "song",
        .artist = "artist",
        .album = "album",
        .tags = "calm",
        .filters = {{"mood", "calm"}}});

    EXPECT_EQ(gui::apply_track_view({row}, nullptr, query, {}), std::vector{row});

    query.filters.push_back({"mood", "calm", library::TagFilterMode::exclude});
    EXPECT_TRUE(gui::apply_track_view({row}, nullptr, query, {}).empty());

    query.filters.clear();
    query.album = "different";
    EXPECT_TRUE(gui::apply_track_view({row}, nullptr, query, {}).empty());
}

TEST(TrackViewPipeline, UsesSnapshotThenIndexThenRepositoryThenRowFallback)
{
    library::InMemoryRepository repository;
    app::SearchIndex search;
    const auto snapshot_record = record("snapshot", "Match");
    const auto indexed_record = record("indexed", "Match");
    const auto stored_record = record("stored", "Match");
    const auto fallback_record = record("fallback", "Match");
    const auto rejected_record = record("rejected", "Other");
    const std::map<library::MediaId, library::SearchFields> fields{
        {snapshot_record.id, library::search_fields_of(snapshot_record)}
    };

    auto changed = snapshot_record;
    changed.embedded.title = "Changed";
    ASSERT_TRUE(repository.put_track(changed));
    ASSERT_TRUE(search.upsert(changed));
    ASSERT_TRUE(search.upsert(indexed_record));
    ASSERT_TRUE(search.upsert(rejected_record));
    ASSERT_TRUE(repository.put_track(stored_record));

    std::vector<gui::TrackRow> rows;
    for (const auto& item :
        {snapshot_record, indexed_record, stored_record, fallback_record, rejected_record}) {
        rows.push_back(gui::present_record(library::PlaylistEntryId{item.id.value}, item));
        rows.back().metadata.title = item.id == fallback_record.id ? "Match" : "Stale row";
    }
    const auto expected = std::vector(rows.begin(), rows.end() - 1);
    library::SearchQuery query;
    query.title = "match";

    EXPECT_EQ(gui::apply_track_view(rows, &repository, query, {}, &search, &fields), expected);
    EXPECT_EQ(gui::apply_track_view(rows, &repository, query, {}, &search),
        (std::vector<gui::TrackRow>{rows[1], rows[2], rows[3]}));
}

TEST(TrackViewPipeline, SortsOnlyMatchesAndReturnsNoPartialResultWhenCancelled)
{
    const auto later = gui::present_record(engine::QueueEntryId{1}, record("b", "Song B"));
    const auto earlier = gui::present_record(engine::QueueEntryId{2}, record("a", "Song A"));
    const auto excluded = gui::present_record(engine::QueueEntryId{3}, record("x", "Other"));
    const std::vector source{later, excluded, earlier};
    library::SearchQuery query;
    query.title = "song";

    EXPECT_EQ(gui::apply_track_view(source, nullptr, query,
                  {
                      {.field = gui::TrackSortField::title, .tag_key = {}}
    }),
        (std::vector{earlier, later}));
    EXPECT_EQ(source, (std::vector{later, excluded, earlier}));

    std::stop_source cancelled;
    cancelled.request_stop();
    EXPECT_TRUE(gui::apply_track_view(source, nullptr, {}, {}, nullptr, nullptr,
        cancelled.get_token())
            .empty());
}

TEST(TrackCapabilities, RestrictsCommandsToTheOwningDocument)
{
    using gui::TrackCapabilities;
    using gui::TrackCommand;
    const std::vector commands{TrackCommand::play, TrackCommand::remove, TrackCommand::transfer,
        TrackCommand::move, TrackCommand::apply_order, TrackCommand::append,
        TrackCommand::transfer_destination};

    for (const auto command : commands) {
        EXPECT_FALSE(TrackCapabilities{}.supports(command));
        EXPECT_TRUE(TrackCapabilities::all().supports(command));
        EXPECT_EQ(TrackCapabilities::queue().supports(command),
            command != TrackCommand::move && command != TrackCommand::append);
        EXPECT_EQ(TrackCapabilities::playlist().supports(command), command != TrackCommand::remove);
        EXPECT_EQ(TrackCapabilities::workspace().supports(command),
            command != TrackCommand::remove && command != TrackCommand::move);
    }
    EXPECT_EQ(TrackCapabilities{}, TrackCapabilities{});
    EXPECT_NE(TrackCapabilities::queue(), TrackCapabilities::playlist());
}

} // namespace
