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

#include <wiola/app/metadata/metadata_edit_service.hpp>

#include <allocation_failure.hpp>
#include <gtest/gtest.h>
#include <memory>
#include <new>
#include <optional>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>

namespace {

class MetadataEditService : public ::testing::Test {
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

static_assert(!std::is_copy_constructible_v<wiola::app::MetadataEditService>);
static_assert(!std::is_move_constructible_v<wiola::app::MetadataEditService>);

TEST_F(MetadataEditService, InvalidEmptyAndUnselectedExpectationsDoNotStartAnotherEdit)
{
    using namespace wiola;
    int writes = 0;
    app::MetadataEditService service{repository,
        [&](const auto&) {
            ++writes;
            return true;
        },
        [&](const auto& value) { outcomes.push_back(value); }};
    library::StandardMetadataPatch patch;
    const auto empty = service.apply({media}, patch);
    EXPECT_EQ(empty.error, app::StandardMetadataError::empty_edit);
    EXPECT_FALSE(empty.succeeded());
    patch.title = std::string(library::max_metadata_text_bytes + 1, 'x');
    const auto invalid = service.apply({media}, patch);
    EXPECT_EQ(invalid.error, app::StandardMetadataError::invalid_edit);
    EXPECT_FALSE(invalid.succeeded());
    EXPECT_EQ(writes, 0);
    patch.title = "Valid";
    const std::map<library::MediaId, app::ExpectedMediaState> unrelated{
        {{"not-selected"}, {.metadata_revision = 99}}
    };
    EXPECT_TRUE(service.apply({media}, patch, unrelated).succeeded());
    EXPECT_EQ(writes, 1);
    EXPECT_EQ(outcomes.size(), 3U);
}

TEST_F(MetadataEditService, UsesTheSuppliedFingerprintReaderForCapturedTargets)
{
    using namespace wiola;
    auto record = *repository.track(media);
    record.locations = {"opaque-location"};
    record.fingerprint = "captured";
    ASSERT_TRUE(repository.put_track(record));
    auto session = app::MetadataEditSession::capture({repository.track(media)});
    library::StandardMetadataPatch patch;
    patch.title = "Draft";
    session.set_draft(patch);
    int reads = 0;
    std::string current = "changed";
    app::MetadataEditService service{repository, [](const auto&) { return true; }, {},
        [&](const auto& path) -> std::optional<std::string> {
            EXPECT_EQ(path, record.locations.front());
            ++reads;
            return current;
        }};
    EXPECT_EQ(service.apply(session).conflict, app::MetadataConflictKind::content_changed);
    EXPECT_EQ(repository.track(media)->display_title(), "Embedded");
    current = record.fingerprint;
    EXPECT_TRUE(service.apply(session).succeeded());
    EXPECT_EQ(reads, 2);
    EXPECT_EQ(repository.track(media)->display_title(), "Draft");
}

TEST_F(MetadataEditService, ReaderMayReleaseTheCallersDraftAfterItsInputsHaveBeenCaptured)
{
    using namespace wiola;
    auto record = *repository.track(media);
    record.locations = {"opaque-location"};
    record.fingerprint = std::string(128, 'f');
    ASSERT_TRUE(repository.put_track(record));

    struct Request {
        std::vector<library::MediaId> media;
        library::StandardMetadataPatch patch;
        std::map<library::MediaId, app::ExpectedMediaState> expected;
    };

    auto request = std::make_unique<Request>();
    request->media = {media};
    request->patch.title = std::string(128, 't');
    request->expected.emplace(media,
        app::ExpectedMediaState{record.metadata_revision, record.content_revision,
            record.fingerprint});
    app::MetadataEditService service{repository, [](const auto&) { return true; }, {},
        [&](const auto&) -> std::optional<std::string> {
            request.reset();
            return record.fingerprint;
        }};

    const auto outcome = service.apply(request->media, request->patch, request->expected);

    EXPECT_FALSE(request);
    EXPECT_TRUE(outcome.succeeded());
    EXPECT_EQ(repository.track(media)->overrides.title, std::string(128, 't'));
}

TEST_F(MetadataEditService, DisposalInsideAnObserverSuppressesReentrantReceiptDelivery)
{
    using namespace wiola;
    std::shared_ptr<library::CommitReceipt> receipt;
    std::unique_ptr<app::MetadataEditService> service;
    int calls = 0;
    service = std::make_unique<app::MetadataEditService>(
        repository,
        [&](const auto&) {
            const auto revision = repository.staged_delta()->changes.revision;
            receipt = std::make_shared<library::CommitReceipt>(revision);
            return library::CommitResult::accepted(revision, receipt);
        },
        [&](const auto&) {
            ++calls;
            service.reset();
            receipt->notify(true);
        }
    );
    library::StandardMetadataPatch patch;
    patch.title = "Retained";

    const auto outcome = service->apply({media}, patch);

    EXPECT_FALSE(service);
    EXPECT_TRUE(outcome.succeeded());
    EXPECT_TRUE(receipt->durable());
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(repository.track(media)->display_title(), "Retained");
}

#if WIOLA_TEST_ALLOCATOR_REPLACEMENT
TEST_F(MetadataEditService, AllocationFailuresRespectAcceptanceAndPreserveTheNextEdit)
{
    using namespace wiola;
    bool observed_failure = false;
    for (std::size_t after = 0;; ++after) {
        library::InMemoryRepository records;
        auto original = *repository.track(media);
        ASSERT_TRUE(records.put_track(original));
        bool accepted = false;
        app::MetadataEditService service{records, [&](const auto&) {
                                             accepted = true;
                                             return true;
                                         }};
        library::StandardMetadataPatch patch;
        patch.title = std::string(library::max_metadata_text_bytes / 2, 'x');
        bool failed = false;
        {
            const wiola::testing::AllocationFailure failure{after};
            try {
                static_cast<void>(service.apply({media}, patch));
            } catch (const std::bad_alloc&) {
                failed = true;
            }
        }
        if (failed) {
            observed_failure = true;
            if (!accepted) {
                EXPECT_EQ(*records.track(media), original);
                EXPECT_EQ(records.change_revision(), 0U);
                EXPECT_TRUE(service.apply({media}, patch).succeeded());
            }
        }
        EXPECT_EQ(records.track(media)->overrides.title, *patch.title.value());
        EXPECT_EQ(records.track(media)->metadata_revision, original.metadata_revision + 1);
        if (!failed) {
            EXPECT_TRUE(observed_failure);
            break;
        }
    }
}
#endif

TEST_F(MetadataEditService, CapturedDraftControlsConflictCheckingAndExplicitOverwrite)
{
    using namespace wiola;
    auto session = app::MetadataEditSession::capture({repository.track(media)});
    library::StandardMetadataPatch patch;
    patch.title = "Captured draft";
    session.set_draft(patch);
    auto updated = *repository.track(media);
    ++updated.metadata_revision;
    ASSERT_TRUE(repository.put_track(updated));
    app::MetadataEditService service{repository, [](const auto&) { return true; }};
    EXPECT_EQ(service.apply(session).state, app::MetadataEditState::conflicted);
    EXPECT_EQ(repository.track(media)->display_title(), "Embedded");
    EXPECT_TRUE(service.apply(session, app::MetadataConflictPolicy::overwrite_current).succeeded());
    EXPECT_EQ(repository.track(media)->display_title(), "Captured draft");
}

TEST_F(MetadataEditService, RejectsWrongThreadEditsWithoutPublishingOrChangingRecords)
{
    using namespace wiola;
    int calls = 0;
    app::MetadataEditService service{repository, [](const auto&) { return true; },
        [&](const auto&) { ++calls; }};
    library::StandardMetadataPatch patch;
    patch.title = "Wrong thread";
    auto session = app::MetadataEditSession::capture({repository.track(media)});
    session.set_draft(patch);
    std::jthread worker{[&] {
        EXPECT_THROW(static_cast<void>(service.apply({media}, patch)), std::logic_error);
        EXPECT_THROW(static_cast<void>(service.apply(session)), std::logic_error);
        EXPECT_THROW(static_cast<void>(service.reset({media})), std::logic_error);
    }};
    worker.join();
    EXPECT_EQ(calls, 0);
    EXPECT_EQ(repository.track(media)->display_title(), "Embedded");
    EXPECT_TRUE(service.apply(session).succeeded());
}

TEST_F(MetadataEditService, ObserverMayStartANewEditAfterThePreviousTransactionFinishes)
{
    using namespace wiola;
    app::MetadataEditService* active = nullptr;
    bool entered = false;
    std::optional<app::MetadataEditOutcome> nested;
    app::MetadataEditService service{repository, [](const auto&) { return true; },
        [&](const auto&) {
            if (!std::exchange(entered, true)) {
                library::StandardMetadataPatch patch;
                patch.artist = "Nested artist";
                nested = active->apply({media}, patch);
            }
        }};
    active = &service;
    library::StandardMetadataPatch patch;
    patch.title = "Outer title";

    const auto outer = service.apply({media}, patch);

    ASSERT_TRUE(outer.succeeded());
    ASSERT_TRUE(nested);
    EXPECT_TRUE(nested->succeeded());
    EXPECT_LT(outer.revision, nested->revision);
    EXPECT_EQ(repository.track(media)->display_title(), "Outer title");
    EXPECT_EQ(repository.track(media)->overrides.artist, "Nested artist");
}

TEST_F(MetadataEditService, ObserverMayDisposeTheServiceAfterTheEditIsAccepted)
{
    using namespace wiola;
    std::unique_ptr<app::MetadataEditService> service;
    int calls = 0;
    service = std::make_unique<app::MetadataEditService>(
        repository, [](const auto&) { return true; },
        [&](const auto&) {
            ++calls;
            service.reset();
        }
    );
    library::StandardMetadataPatch patch;
    patch.title = "Retained";

    const auto outcome = service->apply({media}, patch);

    EXPECT_FALSE(service);
    EXPECT_TRUE(outcome.succeeded());
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(repository.track(media)->display_title(), "Retained");
}

TEST_F(MetadataEditService, DisposedServiceDoesNotDeliverLateReceiptNotifications)
{
    using namespace wiola;
    std::shared_ptr<library::CommitReceipt> receipt;
    {
        app::MetadataEditService service{repository,
            [&](const auto&) {
                const auto revision = repository.staged_delta()->changes.revision;
                receipt = std::make_shared<library::CommitReceipt>(revision);
                return library::CommitResult::accepted(revision, receipt);
            },
            [&](const auto& value) { outcomes.push_back(value); }};
        library::StandardMetadataPatch patch;
        patch.title = "Retained";
        ASSERT_TRUE(service.apply({media}, patch).succeeded());
    }
    const auto before = outcomes.size();

    receipt->notify(false);
    receipt->notify(true);

    EXPECT_EQ(outcomes.size(), before);
    EXPECT_EQ(repository.track(media)->display_title(), "Retained");
}

TEST_F(MetadataEditService, ObserverFailureCannotRollBackAnAcceptedEdit)
{
    using namespace wiola;
    app::MetadataEditService service{repository, [](const auto&) { return true; },
        [](const auto&) { throw std::runtime_error{"observer failed"}; }};
    library::StandardMetadataPatch patch;
    patch.title = "Retained";

    EXPECT_THROW(static_cast<void>(service.apply({media}, patch)), std::runtime_error);

    EXPECT_EQ(repository.track(media)->display_title(), "Retained");
    EXPECT_EQ(repository.change_revision(), 1U);
}

TEST_F(MetadataEditService, MutableObserversAndPersistenceKeepTheirIdentityAcrossOperations)
{
    using namespace wiola;
    std::vector<int> writes, notifications;
    std::vector<std::shared_ptr<library::CommitReceipt>> receipts;
    app::MetadataEditService service{repository,
        [count = 0, &writes, &receipts, this](const auto&) mutable {
            writes.push_back(++count);
            const auto revision = repository.staged_delta()->changes.revision;
            auto receipt = std::make_shared<library::CommitReceipt>(revision);
            receipts.push_back(receipt);
            return library::CommitResult::accepted(revision, receipt);
        },
        [count = 0, &notifications](const auto&) mutable { notifications.push_back(++count); }};
    library::StandardMetadataPatch patch;
    patch.title = "First";
    ASSERT_TRUE(service.apply({media}, patch).succeeded());
    receipts.back()->notify(false);
    receipts.back()->notify(true);
    patch.title = "Second";
    ASSERT_TRUE(service.apply({media}, patch).succeeded());
    receipts.back()->notify(true);

    EXPECT_EQ(writes, (std::vector<int>{1, 2}));
    for (std::size_t i = 0; i != notifications.size(); ++i)
        EXPECT_EQ(notifications[i], static_cast<int>(i + 1));
}

TEST_F(MetadataEditService, PublishesTheFinalOutcomeAfterTheTransactionFinishes)
{
    wiola::library::StandardMetadataPatch edit;
    edit.title = "Optimistic";
    wiola::app::MetadataEditService commands{repository,
        [](const wiola::library::LibraryRepository&) { return true; },
        [this](const wiola::app::MetadataEditOutcome& outcome) {
            EXPECT_EQ(repository.track(media)->overrides.title, "Optimistic");
            outcomes.push_back(outcome);
        }};

    const auto result = commands.apply({media}, edit);
    ASSERT_EQ(outcomes.size(), 1U);
    EXPECT_EQ(outcomes.front().state, wiola::app::MetadataEditState::succeeded);
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
    ASSERT_EQ(outcomes.size(), 1U);
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
    ASSERT_EQ(outcomes.size(), 1U);
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
    EXPECT_FALSE(outcomes.back().retained_changes);
    EXPECT_EQ(repository.track(media)->display_title(), "B");
}

} // namespace
