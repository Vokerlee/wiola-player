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

#include <app/metadata/metadata_edit_service.hpp>

#include <gtest/gtest.h>

namespace {

class MetadataBatchEdit : public testing::Test {
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

TEST_F(MetadataBatchEdit, AppliesOnlyChangedRecordsAndReportsMissingTracks)
{
    wiola::app::MetadataEditService commands{repository,
        [this](const wiola::library::LibraryRepository&) {
            ++saves;
            return true;
        }};
    wiola::library::StandardMetadataPatch edit;
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

TEST_F(MetadataBatchEdit, RollsBackEveryChangedRecordWhenSavingFails)
{
    wiola::app::MetadataEditService commands{repository,
        [](const wiola::library::LibraryRepository&) { return false; }};
    wiola::library::StandardMetadataPatch edit;
    edit.title = "Cannot save";

    const auto result = commands.apply({first, second}, edit);
    EXPECT_EQ(result.error, wiola::app::StandardMetadataError::persistence_failed);
    EXPECT_EQ(result.changed, 0);
    EXPECT_FALSE(repository.track(first)->overrides.title.has_value());
    EXPECT_FALSE(repository.track(second)->overrides.title.has_value());
    EXPECT_EQ(repository.track(first)->metadata_revision, 0);
    EXPECT_EQ(repository.track(second)->metadata_revision, 0);
}

TEST_F(MetadataBatchEdit, PublishesOneCommittedBatchForAllChangedMedia)
{
    std::vector<wiola::library::LibraryChangeBatch> batches;
    auto subscription = repository.observe([&](const auto& batch) { batches.push_back(batch); });
    wiola::app::MetadataEditService commands{repository,
        [](const wiola::library::LibraryRepository&) { return true; }};
    wiola::library::StandardMetadataPatch patch;
    patch.genre = "Jazz";
    ASSERT_TRUE(commands.apply({first, second, first}, patch).succeeded());
    ASSERT_EQ(batches.size(), 1U);
    ASSERT_EQ(batches.front().media.size(), 2U);
    EXPECT_EQ(batches.front().media[0].media, first);
    EXPECT_EQ(batches.front().media[1].media, second);
    EXPECT_TRUE(batches.front().media[0].includes(wiola::library::MediaChangeFields::overrides));
    EXPECT_EQ(commands.apply({first, second}, patch).changed, 0U);
    EXPECT_EQ(batches.size(), 1U);
    wiola::app::MetadataEditService failing{repository,
        [](const wiola::library::LibraryRepository&) { return false; }};
    patch.genre = "Rejected";
    EXPECT_FALSE(failing.apply({first, second}, patch).succeeded());
    EXPECT_EQ(batches.size(), 1U);
}

} // namespace
