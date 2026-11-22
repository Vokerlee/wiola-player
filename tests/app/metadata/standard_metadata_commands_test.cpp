/**
 * @file
 * @brief Application-level standard metadata override regression tests.
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

#include <app/metadata/standard_metadata_commands.hpp>

#include <library/metadata.hpp>

#include <gtest/gtest.h>

namespace {

class StandardMetadataCommands : public testing::Test {
protected:
    void SetUp() override
    {
        wiola::library::TrackRecord track;
        track.id = media;
        track.embedded.title = "Embedded";
        track.embedded.genre = "Rock";
        track.embedded.year = 2020;
        track.embedded.track_number = 2;
        ASSERT_TRUE(repository.put_track(track));
    }

    wiola::library::MediaId media{"media"};
    wiola::library::InMemoryRepository repository;
    int saves{0};
};

TEST_F(StandardMetadataCommands, AppliesFieldsImmediatelyAndSupportsNumericClearing)
{
    wiola::app::StandardMetadataCommands commands{repository,
        [this](const wiola::library::LibraryRepository&) {
            ++saves;
            return true;
        }};

    wiola::library::StandardFieldOverrides edit;
    edit.title = "User title";
    edit.genre = "Jazz";
    edit.comment = "Comment";
    edit.composer = "Composer";
    edit.year = 2026;
    edit.track_number = 4;
    edit.disc_number = 2;
    const auto applied = commands.apply(media, edit);

    ASSERT_TRUE(applied.succeeded());
    EXPECT_TRUE(applied.changed);
    const auto resolved = wiola::library::resolve_metadata(*repository.track(media));
    EXPECT_EQ(resolved.title, "User title");
    EXPECT_EQ(resolved.genre, "Jazz");
    EXPECT_EQ(resolved.comment, "Comment");
    EXPECT_EQ(resolved.composer, "Composer");
    EXPECT_EQ(resolved.year, 2026);
    EXPECT_EQ(resolved.track_number, 4);
    EXPECT_EQ(resolved.disc_number, 2);
    EXPECT_EQ(repository.track(media)->embedded.title, "Embedded");
    EXPECT_EQ(repository.track(media)->metadata_revision, 1);

    wiola::library::StandardFieldOverrides clear;
    clear.year = std::optional<std::uint32_t>{};
    clear.track_number = std::optional<std::uint32_t>{};
    ASSERT_TRUE(commands.apply(media, clear).succeeded());
    EXPECT_FALSE(wiola::library::resolve_metadata(*repository.track(media)).year.has_value());
    EXPECT_FALSE(
        wiola::library::resolve_metadata(*repository.track(media)).track_number.has_value()
    );
    EXPECT_EQ(saves, 2);

    EXPECT_FALSE(commands.apply(media, clear).changed);
    EXPECT_EQ(saves, 2);
}

TEST_F(StandardMetadataCommands, RejectsEmptyAndMissingEdits)
{
    wiola::app::StandardMetadataCommands commands{repository,
        [this](const wiola::library::LibraryRepository&) {
            ++saves;
            return true;
        }};

    EXPECT_EQ(commands.apply(media, {}).error, wiola::app::StandardMetadataError::empty_edit);
    EXPECT_EQ(commands.apply(wiola::library::MediaId{"missing"}, {}).error,
        wiola::app::StandardMetadataError::missing_media);
    EXPECT_EQ(saves, 0);
}

TEST_F(StandardMetadataCommands, RollsBackWhenPersistenceFails)
{
    wiola::app::StandardMetadataCommands commands{repository,
        [](const wiola::library::LibraryRepository&) { return false; }};
    wiola::library::StandardFieldOverrides edit;
    edit.title = "Cannot save";

    EXPECT_EQ(commands.apply(media, edit).error,
        wiola::app::StandardMetadataError::persistence_failed);
    EXPECT_FALSE(repository.track(media)->overrides.title.has_value());
    EXPECT_EQ(repository.track(media)->embedded.title, "Embedded");
    EXPECT_EQ(repository.track(media)->metadata_revision, 0);
}

} // namespace
