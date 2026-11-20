/**
 * @file
 * @brief Bulk metadata editing regression tests.
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

#include <app/bulk_metadata_commands.hpp>

#include <gtest/gtest.h>

namespace {

class BulkMetadataCommands : public testing::Test {
protected:
    void SetUp() override
    {
        for (int value = 1; value <= 2; ++value) {
            wiola::library::TrackRecord track;
            track.id = wiola::library::MediaId{"media-" + std::to_string(value)};
            track.embedded.title = "Embedded " + std::to_string(value);
            ASSERT_TRUE(repository.put_track(track));
        }
    }

    wiola::library::InMemoryRepository repository;
    wiola::library::MediaId first{"media-1"};
    wiola::library::MediaId second{"media-2"};
    int saves{0};
};

TEST_F(BulkMetadataCommands, AppliesOnlyChangedRecordsAndReportsMissingTracks)
{
    wiola::app::BulkMetadataCommands commands{repository,
        [this](const wiola::library::LibraryRepository&) {
            ++saves;
            return true;
        }};
    wiola::library::StandardFieldOverrides edit;
    edit.genre = "Jazz";

    const auto result =
        commands.apply({first, second, first, wiola::library::MediaId{"missing"}}, edit);
    ASSERT_TRUE(result.succeeded());
    EXPECT_EQ(result.changed, 2);
    EXPECT_EQ(result.unchanged, 0);
    EXPECT_EQ(result.missing, 1);
    EXPECT_EQ(saves, 1);

    const auto noop = commands.apply({first, second}, edit);
    ASSERT_TRUE(noop.succeeded());
    EXPECT_EQ(noop.changed, 0);
    EXPECT_EQ(noop.unchanged, 2);
    EXPECT_EQ(saves, 1);
}

TEST_F(BulkMetadataCommands, RollsBackEveryChangedRecordWhenSavingFails)
{
    wiola::app::BulkMetadataCommands commands{repository,
        [](const wiola::library::LibraryRepository&) { return false; }};
    wiola::library::StandardFieldOverrides edit;
    edit.title = "Cannot save";

    const auto result = commands.apply({first, second}, edit);
    EXPECT_EQ(result.error, wiola::app::StandardMetadataError::persistence_failed);
    EXPECT_EQ(result.changed, 0);
    EXPECT_FALSE(repository.track(first)->overrides.title.has_value());
    EXPECT_FALSE(repository.track(second)->overrides.title.has_value());
    EXPECT_EQ(repository.track(first)->metadata_revision, 0);
    EXPECT_EQ(repository.track(second)->metadata_revision, 0);
}

} // namespace
