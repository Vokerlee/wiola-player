/**
 * @file
 * @brief Optimistic metadata outcome regression tests.
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

#include <app/optimistic_metadata_edits.hpp>

#include <gtest/gtest.h>

namespace {

class OptimisticMetadataEdits : public testing::Test {
protected:
    void SetUp() override
    {
        wiola::library::TrackRecord track;
        track.id = media;
        track.embedded.title = "Embedded";
        ASSERT_TRUE(repository.put_track(track));
    }

    wiola::library::MediaId media{"media"};
    wiola::library::InMemoryRepository repository;
    std::vector<wiola::app::MetadataEditOutcome> outcomes;
};

TEST_F(OptimisticMetadataEdits, PublishesQueuedChangesBeforePersistenceSucceeds)
{
    wiola::library::StandardFieldOverrides edit;
    edit.title = "Optimistic";
    wiola::app::OptimisticMetadataEdits commands{repository,
        [](const wiola::library::LibraryRepository&) { return true; },
        [this](const wiola::app::MetadataEditOutcome& outcome) {
            if (outcome.state == wiola::app::MetadataEditState::queued)
                EXPECT_EQ(repository.track(media)->overrides.title, "Optimistic");
            outcomes.push_back(outcome);
        }};

    const auto result = commands.submit({media}, edit);
    ASSERT_EQ(outcomes.size(), 2);
    EXPECT_EQ(outcomes[0].state, wiola::app::MetadataEditState::queued);
    EXPECT_EQ(outcomes[1].state, wiola::app::MetadataEditState::succeeded);
    EXPECT_EQ(result.changed, 1);
}

TEST_F(OptimisticMetadataEdits, RollsBackBeforePublishingFailure)
{
    wiola::library::StandardFieldOverrides edit;
    edit.title = "Cannot save";
    wiola::app::OptimisticMetadataEdits commands{repository,
        [](const wiola::library::LibraryRepository&) { return false; },
        [this](const wiola::app::MetadataEditOutcome& outcome) { outcomes.push_back(outcome); }};

    const auto result = commands.submit({media}, edit);
    ASSERT_EQ(outcomes.size(), 2);
    EXPECT_EQ(outcomes.back().state, wiola::app::MetadataEditState::failed);
    EXPECT_EQ(result.error, wiola::app::StandardMetadataError::persistence_failed);
    EXPECT_FALSE(repository.track(media)->overrides.title.has_value());
}

TEST_F(OptimisticMetadataEdits, RejectsRevisionConflictsBeforeChangingRecords)
{
    wiola::library::StandardFieldOverrides edit;
    edit.title = "Conflict";
    wiola::app::OptimisticMetadataEdits commands{repository,
        [](const wiola::library::LibraryRepository&) { return true; },
        [this](const wiola::app::MetadataEditOutcome& outcome) { outcomes.push_back(outcome); }};

    const auto result = commands.submit(
        {
            media
    },
        edit, {{media, 99}}
    );
    ASSERT_EQ(outcomes.size(), 1);
    EXPECT_EQ(result.state, wiola::app::MetadataEditState::conflicted);
    EXPECT_FALSE(repository.track(media)->overrides.title.has_value());
    EXPECT_EQ(repository.track(media)->metadata_revision, 0);
}

} // namespace
