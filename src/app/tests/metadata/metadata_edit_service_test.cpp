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

#include <app/metadata/metadata_edit_service.hpp>

#include <gtest/gtest.h>

namespace {

class MetadataEditService : public testing::Test {
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

TEST_F(MetadataEditService, PublishesQueuedChangesBeforePersistenceSucceeds)
{
    wiola::library::StandardMetadataPatch edit;
    edit.title = "Optimistic";
    wiola::app::MetadataEditService commands{repository,
        [](const wiola::library::LibraryRepository&) { return true; },
        [this](const wiola::app::MetadataEditOutcome& outcome) {
            if (outcome.state == wiola::app::MetadataEditState::queued)
                EXPECT_EQ(repository.track(media)->overrides.title, "Optimistic");
            outcomes.push_back(outcome);
        }};

    const auto result = commands.apply({media}, edit);
    ASSERT_EQ(outcomes.size(), 2);
    EXPECT_EQ(outcomes[0].state, wiola::app::MetadataEditState::queued);
    EXPECT_EQ(outcomes[1].state, wiola::app::MetadataEditState::succeeded);
    EXPECT_EQ(result.changed, 1);
}

TEST_F(MetadataEditService, RollsBackBeforePublishingFailure)
{
    wiola::library::StandardMetadataPatch edit;
    edit.title = "Cannot save";
    wiola::app::MetadataEditService commands{repository,
        [](const wiola::library::LibraryRepository&) { return false; },
        [this](const wiola::app::MetadataEditOutcome& outcome) { outcomes.push_back(outcome); }};

    const auto result = commands.apply({media}, edit);
    ASSERT_EQ(outcomes.size(), 2);
    EXPECT_EQ(outcomes.back().state, wiola::app::MetadataEditState::failed);
    EXPECT_EQ(result.error, wiola::app::StandardMetadataError::persistence_failed);
    EXPECT_FALSE(repository.track(media)->overrides.title.has_value());
}

TEST_F(MetadataEditService, RejectsRevisionConflictsBeforeChangingRecords)
{
    wiola::library::StandardMetadataPatch edit;
    edit.title = "Conflict";
    wiola::app::MetadataEditService commands{repository,
        [](const wiola::library::LibraryRepository&) { return true; },
        [this](const wiola::app::MetadataEditOutcome& outcome) { outcomes.push_back(outcome); }};

    const auto result = commands.apply(
        {
            media
    },
        edit, {{media, wiola::app::ExpectedMediaState{.metadata_revision = 99}}}
    );
    ASSERT_EQ(outcomes.size(), 1);
    EXPECT_EQ(result.state, wiola::app::MetadataEditState::conflicted);
    EXPECT_FALSE(repository.track(media)->overrides.title.has_value());
    EXPECT_EQ(repository.track(media)->metadata_revision, 0);
}

TEST_F(MetadataEditService, ReloadClearsApplicationOverridesTransactionally)
{
    wiola::library::StandardMetadataPatch edit;
    edit.title = "Application";
    wiola::app::MetadataEditService commands{repository,
        [](const wiola::library::LibraryRepository&) { return true; },
        [this](const wiola::app::MetadataEditOutcome& outcome) { outcomes.push_back(outcome); }};
    ASSERT_EQ(commands.apply({media}, edit).state, wiola::app::MetadataEditState::succeeded);
    outcomes.clear();

    const auto result = commands.reset({media});
    ASSERT_EQ(outcomes.size(), 2);
    EXPECT_EQ(result.state, wiola::app::MetadataEditState::succeeded);
    EXPECT_EQ(result.changed, 1);
    EXPECT_TRUE(repository.track(media)->overrides == wiola::library::StandardFieldOverrides{});
}

TEST_F(MetadataEditService, NoopAndResetUseTheSameBatchContractWithoutSaving)
{
    int saves = 0;
    wiola::app::MetadataEditService service{repository,
        [&](const auto&) {
            ++saves;
            return true;
        },
        [&](const auto& outcome) { outcomes.push_back(outcome); }};
    auto result = service.reset({media, media, wiola::library::MediaId{"missing"}});
    EXPECT_TRUE(result.succeeded());
    EXPECT_EQ(result.changed, 0);
    EXPECT_EQ(result.unchanged, 1);
    EXPECT_EQ(result.missing, 1);
    EXPECT_EQ(saves, 0);
    ASSERT_EQ(outcomes.size(), 1);
    EXPECT_EQ(outcomes.front().state, wiola::app::MetadataEditState::succeeded);
}

TEST_F(MetadataEditService,
    AcceptedEditsReportDurabilitySeparatelyAndLateFailureCannotUndoANewerEdit)
{
    using namespace wiola;
    std::vector<std::shared_ptr<library::CommitReceipt>> receipts;
    app::MetadataEditService commands{repository,
        [&](const auto&) {
            const auto revision = repository.staged_delta()->changes.revision;
            auto receipt = std::make_shared<library::CommitReceipt>(revision);
            receipts.push_back(receipt);
            return library::CommitResult::accepted(revision, receipt);
        },
        [this](const auto& outcome) { outcomes.push_back(outcome); }};
    library::StandardMetadataPatch patch;
    patch.title = "A";
    const auto first = commands.apply({media}, patch);
    ASSERT_EQ(first.state, app::MetadataEditState::accepted);
    EXPECT_TRUE(first.succeeded());
    patch.title = "B";
    const auto second = commands.apply({media}, patch);
    ASSERT_EQ(second.state, app::MetadataEditState::accepted);
    ASSERT_EQ(receipts.size(), 2);
    receipts[0]->notify(false);
    EXPECT_EQ(repository.track(media)->display_title(), "B");
    EXPECT_EQ(outcomes.back().state, app::MetadataEditState::failed);
    EXPECT_TRUE(outcomes.back().retained_changes);
    EXPECT_EQ(outcomes.back().changed, 1);
    EXPECT_EQ(outcomes.back().revision, first.revision);
    receipts[0]->notify(true);
    receipts[1]->notify(true);
    EXPECT_EQ(outcomes.back().state, app::MetadataEditState::succeeded);
    EXPECT_EQ(outcomes.back().revision, second.revision);
    EXPECT_EQ(repository.track(media)->display_title(), "B");
}

} // namespace
