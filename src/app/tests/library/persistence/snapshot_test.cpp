/**
 * @file
 * @brief Snapshot preparation, accepted records and retry tests.
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

#include "fixture.hpp"

#include <new>
#include <stdexcept>

namespace wiola::app::test::persistence {

TEST_F(LibraryPersistenceCore, InitialSnapshotAndRemovalFactsReachTheSuppliedWriter)
{
    library::LibrarySnapshot initial;
    auto first = revision(1, "retained")->tracks.front();
    auto removed = revision(1, "removed")->tracks.front();
    initial.tracks = {first, removed};
    initial.playlists = {
        {.id = {"playlist"}, .name = "Saved"}
    };
    initial.workspaces = {
        {.id = {"workspace"}, .root = "observed"}
    };
    start(initial);

    auto delta = revision(1, "added");
    delta->changes.removed_media = {removed.id};
    delta->changes.removed_playlists = {initial.playlists.front().id};
    delta->changes.removed_workspaces = {initial.workspaces.front().id};

    const auto accepted = writer->accept(delta);

    ASSERT_TRUE(accepted);
    ASSERT_TRUE(writer->flush().succeeded());

    const auto saved = store.last_snapshot();

    ASSERT_EQ(saved.tracks.size(), 2U);
    EXPECT_EQ(saved.tracks.front().id.value, "added");
    EXPECT_EQ(saved.tracks.back(), first);
    EXPECT_TRUE(saved.playlists.empty());
    EXPECT_TRUE(saved.workspaces.empty());
}

TEST_F(LibraryPersistenceCore, FailedAttemptsRetainBothAcceptedEditsForSuccessfulRetry)
{
    store.block();
    start();

    const auto first = writer->accept(revision(1, "first"));

    ASSERT_TRUE(first);
    ASSERT_TRUE(store.wait_entered());

    const auto second = writer->accept(revision(2, "second"));

    ASSERT_TRUE(second);

    std::vector<bool> first_events, second_events;
    first.receipt->observe([&](auto event) { first_events.push_back(event.durable); });
    second.receipt->observe([&](auto event) { second_events.push_back(event.durable); });

    bool attempt_saved = true;
    writer->when_durable(2, [&](bool saved) { attempt_saved = saved; });
    store.release({app::SnapshotWriteError::io});
    EXPECT_FALSE(writer->flush().succeeded());
    EXPECT_FALSE(attempt_saved);
    EXPECT_EQ(writer->pending_revision(), 2U);
    EXPECT_EQ(writer->durable_revision(), 0U);

    store.release();
    writer->retry();
    ASSERT_TRUE(writer->flush().succeeded());
    ASSERT_FALSE(first_events.empty());
    ASSERT_FALSE(second_events.empty());
    EXPECT_FALSE(first_events.front());
    EXPECT_FALSE(second_events.front());
    EXPECT_TRUE(first_events.back());
    EXPECT_TRUE(second_events.back());
    EXPECT_EQ(store.last_snapshot().tracks.size(), 2U);
    EXPECT_EQ(writer->durable_revision(), 2U);
}

TEST_F(LibraryPersistenceCore, RuntimeFailureAtTheDriverBoundaryLeavesTheRevisionRetryable)
{
    store.release({}, std::make_exception_ptr(std::runtime_error{"controlled storage failure"}));
    start();

    const auto accepted = writer->accept(revision(1));

    ASSERT_TRUE(accepted);
    EXPECT_EQ(writer->flush().error, app::SnapshotWriteError::io);
    EXPECT_FALSE(accepted.receipt->durable());

    store.release();
    ASSERT_TRUE(writer->flush().succeeded());
    EXPECT_TRUE(accepted.receipt->durable());
}

TEST_F(LibraryPersistenceCore, FlushReportsAnUnseenDriverFailureThenRetriesTheReportedFailure)
{
    store.release({app::SnapshotWriteError::io});
    start();

    const auto accepted = writer->accept(revision(1));

    ASSERT_TRUE(accepted);
    ASSERT_TRUE(delivery.wait_pending(1));

    store.release();

    EXPECT_EQ(writer->flush().error, app::SnapshotWriteError::io);
    EXPECT_FALSE(accepted.receipt->durable());
    ASSERT_TRUE(writer->flush().succeeded());
    EXPECT_TRUE(accepted.receipt->durable());
}

TEST_F(LibraryPersistenceCore, FlushRetriesADriverFailureAlreadyDeliveredByTheOwner)
{
    store.release({app::SnapshotWriteError::io});
    start();

    const auto accepted = writer->accept(revision(1));

    ASSERT_TRUE(accepted);
    ASSERT_TRUE(delivery.wait_pending(1));

    delivery.drain();
    EXPECT_FALSE(accepted.receipt->durable());

    store.release();

    ASSERT_TRUE(writer->flush().succeeded());
    EXPECT_TRUE(accepted.receipt->durable());
}

#if WIOLA_TEST_ALLOCATOR_REPLACEMENT
TEST_F(LibraryPersistenceCore, PartialPreparationFailureRetainsTheEntireAcceptedDelta)
{
    store.block();
    writer = std::make_unique<app::LibraryPersistence>(store, fail_after_first_delivery(delivery),
        library::LibrarySnapshot{});

    auto seed = revision(1, "first");
    seed->tracks.push_back(revision(1, "obsolete")->tracks.front());

    const auto first = writer->accept(seed);

    ASSERT_TRUE(first);
    ASSERT_TRUE(store.wait_entered());

    auto delta = revision(2, "first");
    delta->tracks.front().embedded.title = "Updated";
    ++delta->tracks.front().metadata_revision;
    delta->tracks.push_back(revision(2, "second")->tracks.front());
    delta->changes.removed_media = {{"obsolete"}};

    const auto second = writer->accept(delta);

    ASSERT_TRUE(second);

    store.release();

    EXPECT_EQ(writer->flush().error, app::SnapshotWriteError::io);
    EXPECT_TRUE(first.receipt->durable());
    EXPECT_FALSE(second.receipt->durable());
    EXPECT_EQ(writer->pending_revision(), delta->changes.revision);
    EXPECT_EQ(writer->durable_revision(), seed->changes.revision);
    EXPECT_EQ(store.last_snapshot().tracks, seed->tracks);

    writer->retry();
    ASSERT_TRUE(writer->flush().succeeded());
    EXPECT_TRUE(second.receipt->durable());
    EXPECT_EQ(store.last_snapshot().tracks, delta->tracks);
}
#endif

#if WIOLA_TEST_ALLOCATOR_REPLACEMENT
TEST_F(LibraryPersistenceCore, FlushReportsAnAlreadyCompletedPreparationFailureBeforeRetrying)
{
    store.block();
    writer = std::make_unique<app::LibraryPersistence>(store, fail_after_first_delivery(delivery),
        library::LibrarySnapshot{});
    ASSERT_TRUE(writer->accept(revision(1, "first")));
    ASSERT_TRUE(store.wait_entered());

    const auto second = writer->accept(revision(2, "second"));

    ASSERT_TRUE(second);

    store.release();
    ASSERT_TRUE(delivery.wait_pending(2));

    EXPECT_EQ(writer->flush().error, app::SnapshotWriteError::io);
    EXPECT_FALSE(second.receipt->durable());
    ASSERT_TRUE(writer->flush().succeeded());
    EXPECT_TRUE(second.receipt->durable());
    EXPECT_EQ(store.last_snapshot().tracks.size(), 2U);
}

TEST_F(LibraryPersistenceCore, FailedPreparationKeepsCapacityUntilANewerPhysicalResultRecoversIt)
{
    const app::LibraryPersistenceLimits limits{.pending_revisions = 2};

    store.block();
    writer = std::make_unique<app::LibraryPersistence>(store, fail_after_first_delivery(delivery),
        library::LibrarySnapshot{}, app::LibraryPersistence::Observer{}, limits);
    ASSERT_TRUE(writer->accept(revision(1, "first")));
    ASSERT_TRUE(store.wait_entered());

    const auto second = writer->accept(revision(2, "second"));

    ASSERT_TRUE(second);

    store.release();
    EXPECT_EQ(writer->flush().error, app::SnapshotWriteError::io);
    EXPECT_FALSE(second.receipt->durable());
    EXPECT_FALSE(writer->accept(revision(3, "ordinary")));

    auto physical = revision(3, "physical");
    physical->priority = library::CommitPriority::file_result;

    const auto third = writer->accept(physical);

    ASSERT_TRUE(third);
    ASSERT_TRUE(writer->flush().succeeded());

    EXPECT_TRUE(second.receipt->durable());
    EXPECT_TRUE(third.receipt->durable());

    const auto snapshot = store.last_snapshot();

    ASSERT_EQ(snapshot.tracks.size(), 3U);
    EXPECT_EQ(snapshot.tracks.front().id.value, "first");
    EXPECT_EQ(snapshot.tracks[1], physical->tracks.front());
    EXPECT_EQ(snapshot.tracks.back().id.value, "second");
}
#endif

TEST_F(LibraryPersistenceCore, EveryDriverExceptionLeavesTheRevisionRetryable)
{
    struct DriverFailure { };

    for (const auto& failure : {std::make_exception_ptr(std::logic_error{"driver failure"}),
             std::make_exception_ptr(std::bad_alloc{}), std::make_exception_ptr(DriverFailure{})}) {
        store.release({}, failure);
        start();

        const auto delta = revision(1, "retained");
        const auto accepted = writer->accept(delta);

        ASSERT_TRUE(accepted);

        std::vector<bool> attempts;
        accepted.receipt->observe([&](auto event) { attempts.push_back(event.durable); });

        EXPECT_EQ(writer->flush().error, app::SnapshotWriteError::io);
        EXPECT_EQ(writer->pending_revision(), delta->changes.revision);
        EXPECT_EQ(writer->durable_revision(), 0U);
        EXPECT_FALSE(accepted.receipt->durable());

        store.release();
        writer->retry();
        ASSERT_TRUE(writer->flush().succeeded());
        EXPECT_TRUE(accepted.receipt->durable());
        EXPECT_EQ(attempts, (std::vector<bool>{false, true}));

        const auto snapshot = store.last_snapshot();

        ASSERT_EQ(snapshot.tracks.size(), delta->tracks.size());
        EXPECT_EQ(snapshot.tracks.front(), delta->tracks.front());
    }
}

TEST_F(LibraryPersistenceCore, OwnedDeltaUpdatesEverySnapshotCollection)
{
    std::vector<std::uint64_t> acknowledgments;
    start({}, [&](auto number, auto result) {
        EXPECT_TRUE(result.succeeded());
        acknowledgments.push_back(number);
    });

    auto delta = revision(1);
    auto& track = delta->tracks.front();
    track.fingerprint = "observed-fingerprint";
    track.locations = {"observed/track.flac"};
    track.tags = {
        {.id = {"tag"}, .key = "Mood", .value = "Calm"}
    };
    track.overrides.title = "Application title";
    delta->playlists = {
        {.id = {"saved"}, .name = "Saved", .entries = {{.id = {"occurrence"}, .media = track.id}}}
    };
    delta->workspaces = {
        {.id = {"workspace"}, .root = "observed"}
    };
    delta->changes.media = {{.media = track.id}};
    delta->changes.playlists = {delta->playlists.front().id};
    delta->changes.workspaces = {delta->workspaces.front().id};
    ASSERT_TRUE(writer->accept(delta));
    ASSERT_TRUE(writer->flush().succeeded());

    const auto saved = store.last_snapshot();

    EXPECT_EQ(saved.tracks, delta->tracks);
    EXPECT_EQ(saved.playlists, delta->playlists);
    EXPECT_EQ(saved.workspaces, delta->workspaces);
    EXPECT_EQ(acknowledgments, std::vector<std::uint64_t>{1});
}

TEST_F(LibraryPersistenceCore, FlushSavesTheInitialSnapshotWithoutInventingAnEditRevision)
{
    library::LibrarySnapshot initial;
    initial.tracks = revision(1)->tracks;

    int acknowledgments = 0;
    start(initial, [&](auto number, auto result) {
        EXPECT_EQ(number, 0U);
        EXPECT_TRUE(result.succeeded());

        ++acknowledgments;
    });
    ASSERT_TRUE(writer->flush().succeeded());
    EXPECT_EQ(writer->pending_revision(), 0U);
    EXPECT_EQ(writer->durable_revision(), 0U);
    EXPECT_EQ(store.last_snapshot().tracks, initial.tracks);

    writer->retry();
    EXPECT_TRUE(writer->flush().succeeded());
    EXPECT_EQ(acknowledgments, 1);
}

TEST_F(LibraryPersistenceCore, EditsAdmittedWhileAFailureWaitsForDeliverySurviveRecovery)
{
    store.block();
    start();
    const auto first = writer->accept(revision(1, "first"));
    ASSERT_TRUE(first);
    ASSERT_TRUE(store.wait_entered());
    const auto second = writer->accept(revision(2, "second"));
    ASSERT_TRUE(second);
    store.release({app::SnapshotWriteError::io});
    ASSERT_TRUE(delivery.wait_pending(1));

    const auto third = writer->accept(revision(3, "third"));
    ASSERT_TRUE(third);
    delivery.reverse_pending();
    delivery.drain();
    store.release();
    bool saved = false;
    for (int attempt = 0; attempt < 4 && !saved; ++attempt) {
        writer->retry();
        saved = writer->flush().succeeded();
    }

    ASSERT_TRUE(saved);
    EXPECT_EQ(writer->durable_revision(), 3U);
    EXPECT_EQ(writer->pending_revision(), 3U);
    const auto snapshot = store.last_snapshot();
    ASSERT_EQ(snapshot.tracks.size(), 3U);
    EXPECT_EQ(snapshot.tracks[0].id.value, "first");
    EXPECT_EQ(snapshot.tracks[1].id.value, "second");
    EXPECT_EQ(snapshot.tracks[2].id.value, "third");
}

} // namespace wiola::app::test::persistence
