/**
 * @file
 * @brief Layered metadata resolution regression tests.
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

#include <wiola/library/metadata.hpp>

#include <gtest/gtest.h>

namespace {

wiola::library::TrackRecord track()
{
    wiola::library::TrackRecord record;
    record.embedded.title = "Embedded title";
    record.embedded.artist = "Embedded artist";
    record.embedded.album = "Embedded album";
    record.embedded.duration = wiola::units::Time{12.5};
    record.embedded.genre = "Jazz";
    record.embedded.year = 2026;
    record.embedded.comment = "Live recording";
    record.embedded.composer = "Composer";
    record.embedded.track_number = 3;
    record.embedded.disc_number = 2;
    record.tags = {
        {.id = wiola::library::TagId{"tag-1"}, .key = "genre", .value = "Jazz"},
        {.id = wiola::library::TagId{"tag-2"}, .key = "genre", .value = "Live"},
    };

    return record;
}

TEST(Metadata, ResolvesOverridesTagsDurationAndMissingState)
{
    auto record = track();
    record.overrides.title = "User title";
    record.missing = true;

    const auto resolved = wiola::library::resolve_metadata(record);

    EXPECT_EQ(resolved.title, "User title");
    EXPECT_EQ(resolved.artist, "Embedded artist");
    EXPECT_EQ(resolved.album, "Embedded album");
    ASSERT_TRUE(resolved.duration.has_value());
    EXPECT_DOUBLE_EQ(resolved.duration->seconds(), 12.5);
    EXPECT_EQ(resolved.genre, "Jazz");
    EXPECT_EQ(resolved.year, 2026);
    EXPECT_EQ(resolved.comment, "Live recording");
    EXPECT_EQ(resolved.composer, "Composer");
    EXPECT_EQ(resolved.track_number, 3);
    EXPECT_EQ(resolved.disc_number, 2);
    ASSERT_EQ(resolved.tags.size(), 2);
    EXPECT_EQ(resolved.tags[0].value, "Jazz");
    EXPECT_EQ(resolved.tags[1].value, "Live");
    EXPECT_TRUE(resolved.missing);
}

TEST(Metadata, PreservesIntentionalBlankOverrides)
{
    auto record = track();
    record.overrides.title = "";
    record.locations = {"/music/fallback.flac"};

    EXPECT_EQ(wiola::library::resolve_metadata(record).title, "");
}

TEST(Metadata, UsesALocationOnlyWhenNoTitleLayerHasAValue)
{
    auto record = track();
    record.embedded.title.clear();
    record.locations = {"/music/fallback.flac"};

    EXPECT_EQ(wiola::library::resolve_metadata(record).title, "fallback");
}

TEST(Metadata, NarrowTitleResolutionPreservesEveryTitleLayer)
{
    wiola::library::TrackRecord record;

    EXPECT_TRUE(wiola::library::resolve_title(record).empty());

    record.locations = {"missing/file-title.flac"};
    EXPECT_EQ(wiola::library::resolve_title(record), "file-title");

    record.embedded.title = "Embedded";
    EXPECT_EQ(wiola::library::resolve_title(record), "Embedded");

    record.overrides.title = "";
    EXPECT_TRUE(wiola::library::resolve_title(record).empty());

    record.overrides.title = "Override";
    EXPECT_EQ(wiola::library::resolve_title(record), "Override");
    EXPECT_EQ(wiola::library::resolve_metadata(record).title, "Override");
}

TEST(Metadata, ResolvesNonTitleOverridesWithoutModifyingEmbeddedFacts)
{
    auto record = track();
    const auto embedded = record.embedded;

    record.overrides.artist = "Application artist";
    record.overrides.album = "Application album";
    record.overrides.genre = "Fusion";
    record.overrides.year = std::optional<std::uint32_t>{};
    record.overrides.comment = "Application comment";
    record.overrides.composer = "Application composer";
    record.overrides.track_number = std::optional<std::uint32_t>{13};
    record.overrides.disc_number = std::optional<std::uint32_t>{8};

    const auto resolved = wiola::library::resolve_metadata(record);

    EXPECT_EQ(resolved.artist, "Application artist");
    EXPECT_EQ(resolved.album, "Application album");
    EXPECT_EQ(resolved.genre, "Fusion");
    EXPECT_FALSE(resolved.year);
    EXPECT_EQ(resolved.comment, "Application comment");
    EXPECT_EQ(resolved.composer, "Application composer");
    EXPECT_EQ(resolved.track_number, 13U);
    EXPECT_EQ(resolved.disc_number, 8U);
    EXPECT_EQ(record.embedded, embedded);
}

} // namespace
