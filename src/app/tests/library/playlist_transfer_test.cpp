/**
 * @file
 * @brief Transactional playlist transfer regression tests.
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

#include <wiola/app/library/playlist_transfer.hpp>

#include <gtest/gtest.h>
#include <memory>
#include <stdexcept>

namespace {

TEST(PlaylistTransfer, AddsSelectedMediaToTheDestinationWithDuplicateOccurrences)
{
    wiola::library::InMemoryRepository repository;
    wiola::app::PlaylistDestination active;
    const auto destination = repository.create_playlist("Destination");
    ASSERT_TRUE(active.set(destination));

    const wiola::library::MediaId media{"media-1"};
    const wiola::app::MediaTransfer item{.media = media, .location = "/music/track.flac"};
    wiola::app::PlaylistTransfer transfer{repository, active,
        [](const wiola::library::LibraryRepository&) { return true; }};

    const auto result = transfer.add_to_destination({item, item});

    EXPECT_TRUE(result.succeeded());
    EXPECT_EQ(result.added, 2);
    ASSERT_NE(repository.track(media), nullptr);
    EXPECT_EQ(repository.track(media)->locations.front(), item.location);

    const auto* saved = repository.playlist(destination);
    ASSERT_NE(saved, nullptr);
    ASSERT_EQ(saved->entries.size(), 2);
    EXPECT_NE(saved->entries[0].id, saved->entries[1].id);
    EXPECT_EQ(saved->entries[0].media, media);
    EXPECT_EQ(saved->entries[1].media, media);
}

TEST(PlaylistTransfer, SupportsExplicitDestinationsAndRollsBackFailedBatches)
{
    wiola::library::InMemoryRepository repository;
    wiola::app::PlaylistDestination active;
    const auto active_id = repository.create_playlist("Active");
    const auto other_id = repository.create_playlist("Other");
    ASSERT_TRUE(active.set(active_id));

    const wiola::library::MediaId media{"media-1"};
    const wiola::app::MediaTransfer item{.media = media, .location = "/music/track.flac"};
    wiola::app::PlaylistTransfer transfer{repository, active,
        [](const wiola::library::LibraryRepository&) { return true; }};

    EXPECT_TRUE(transfer.add_to(other_id, {item}).succeeded());
    EXPECT_EQ(active.id(), active_id);
    ASSERT_NE(repository.playlist(other_id), nullptr);
    EXPECT_EQ(repository.playlist(other_id)->entries.size(), 1);

    const wiola::library::MediaId rejected{"media-rejected"};
    wiola::app::PlaylistTransfer failing{repository, active,
        [](const wiola::library::LibraryRepository&) { return false; }};
    const auto result = failing.add_to_destination({
        {.media = rejected, .location = "rejected.wav"}
    });

    EXPECT_FALSE(result.succeeded());
    EXPECT_EQ(result.added, 0);
    EXPECT_EQ(repository.track(rejected), nullptr);
    EXPECT_EQ(repository.playlist(active_id)->entries.size(), 0);
    EXPECT_EQ(repository.playlist(other_id)->entries.size(), 1);

    ASSERT_TRUE(transfer.add_to(active_id, {item, item}).succeeded());
    const auto selected = repository.playlist(active_id)->entries.front().id;
    const std::vector<wiola::library::PlaylistEntryId> selected_entries{selected};
    const auto moved = transfer.move_to(active_id, other_id, selected_entries);

    EXPECT_TRUE(moved.succeeded());
    EXPECT_EQ(moved.added, 1);
    EXPECT_EQ(repository.playlist(active_id)->entries.size(), 1);
    EXPECT_EQ(repository.playlist(other_id)->entries.size(), 2);
    EXPECT_NE(repository.playlist(active_id)->entries.front().id, selected);
}

TEST(PlaylistTransfer, PreservesTheMutablePersistenceIdentityAcrossTransfers)
{
    wiola::library::InMemoryRepository repository;
    wiola::app::PlaylistDestination active;
    const auto destination = repository.create_playlist("Destination");
    ASSERT_TRUE(active.set(destination));
    std::vector<int> calls;
    wiola::app::PlaylistTransfer transfer{repository, active, [&, count = 0](const auto&) mutable {
                                              calls.push_back(++count);
                                              return true;
                                          }};
    const wiola::app::MediaTransfer media{.media = {"media"}, .location = {}};
    ASSERT_TRUE(transfer.add_to_destination({media}).succeeded());
    ASSERT_TRUE(transfer.add_to_destination({media}).succeeded());
    EXPECT_EQ(calls, (std::vector{1, 2}));
}

TEST(PlaylistTransfer, EachTransferOwnsItsAcceptedCommitAndDurabilityReceipt)
{
    using namespace wiola;
    library::InMemoryRepository repository;
    app::PlaylistDestination destination;
    const auto playlist = repository.create_playlist("Destination");
    ASSERT_TRUE(destination.set(playlist));
    app::PlaylistTransfer transfer{repository, destination, [&](const auto&) {
                                       const auto revision =
                                           repository.staged_delta()->changes.revision;
                                       return library::CommitResult::accepted(revision,
                                           std::make_shared<library::CommitReceipt>(revision));
                                   }};
    const app::MediaTransfer media{.media = {"media"}, .location = {}};
    const auto first = transfer.add_to_destination({media});
    const auto second = transfer.add_to_destination({media});
    ASSERT_TRUE(first.succeeded());
    ASSERT_TRUE(second.succeeded());
    ASSERT_TRUE(first.commit.receipt);
    ASSERT_TRUE(second.commit.receipt);
    EXPECT_NE(first.commit.revision, second.commit.revision);
    EXPECT_NE(first.commit.receipt, second.commit.receipt);
    EXPECT_FALSE(first.commit.durable());
    second.commit.receipt->notify(true);
    EXPECT_TRUE(second.commit.receipt->durable());
    EXPECT_FALSE(first.commit.receipt->durable());
    first.commit.receipt->notify(true);
    EXPECT_TRUE(first.commit.receipt->durable());
}

TEST(PlaylistTransfer, AnAdmittedTransferMayOutliveItsCommandFacade)
{
    using namespace wiola;
    library::InMemoryRepository repository;
    app::PlaylistDestination destination;
    const auto playlist = repository.create_playlist("Destination");
    ASSERT_TRUE(destination.set(playlist));
    std::unique_ptr<app::PlaylistTransfer> transfer;
    transfer = std::make_unique<app::PlaylistTransfer>(repository, destination, [&](const auto&) {
        transfer.reset();
        return true;
    });
    const auto result = transfer->add_to_destination({
        {.media = {"media"}, .location = {}}
    });
    EXPECT_FALSE(transfer);
    EXPECT_TRUE(result.succeeded());
    EXPECT_EQ(result.added, 1U);
    EXPECT_EQ(repository.playlist(playlist)->entries.size(), 1U);
}

TEST(PlaylistTransfer, MissingDestinationsAndInvalidMediaDoNotPersistOrInventRecords)
{
    using namespace wiola;
    library::InMemoryRepository repository;
    app::PlaylistDestination destination;
    int writes{};
    app::PlaylistTransfer transfer{repository, destination, [&](const auto&) {
                                       ++writes;
                                       return true;
                                   }};
    const app::MediaTransfer media{.media = {"media"}, .location = {}};
    EXPECT_FALSE(transfer.add_to_destination({media}).succeeded());
    EXPECT_FALSE(transfer.add_to({"missing"}, {media}).succeeded());
    const auto playlist = repository.create_playlist("Destination");
    EXPECT_FALSE(transfer.add_to(playlist, {}).succeeded());
    EXPECT_FALSE(transfer
            .add_to(playlist,
                {
                    {.media = {}, .location = {}}
    })
            .succeeded());
    EXPECT_EQ(writes, 0);
    EXPECT_TRUE(repository.tracks().empty());
}

TEST(PlaylistTransfer, MovesOnlyCapturedOccurrencesAndPreservesARejectedBatch)
{
    using namespace wiola;
    library::InMemoryRepository repository;
    app::PlaylistDestination destination;
    const auto source = repository.create_playlist("Source");
    const auto target = repository.create_playlist("Target");
    app::PlaylistTransfer transfer{repository, destination, [](const auto&) { return true; }};
    ASSERT_TRUE(transfer
            .add_to(source,
                {
                    {.media = {"media"}, .location = {}},
                    {.media = {"media"}, .location = {}}
    })
            .succeeded());
    const auto first = repository.playlist(source)->entries.front().id;
    const auto second = repository.playlist(source)->entries.back().id;
    const std::vector captured{first, first, library::PlaylistEntryId{"gone"}};
    const auto moved = transfer.move_to(source, target, captured);
    EXPECT_TRUE(moved.succeeded());
    EXPECT_EQ(moved.added, 1U);
    EXPECT_EQ(repository.playlist(source)->entries.front().id, second);
    const auto before_source = *repository.playlist(source);
    const auto before_target = *repository.playlist(target);
    app::PlaylistTransfer reject{repository, destination, [](const auto&) { return false; }};
    const std::vector remaining{second};
    const auto refused = reject.move_to(source, target, remaining);
    EXPECT_FALSE(refused.succeeded());
    EXPECT_EQ(refused.added, 0U);
    EXPECT_EQ(*repository.playlist(source), before_source);
    EXPECT_EQ(*repository.playlist(target), before_target);
    EXPECT_FALSE(transfer.move_to(source, source, remaining).succeeded());
    EXPECT_FALSE(transfer.move_to(source, {"missing"}, remaining).succeeded());
    EXPECT_FALSE(transfer.move_to({"missing"}, target, remaining).succeeded());
    EXPECT_FALSE(transfer.move_to(source, target, {}).succeeded());
    const std::vector gone{library::PlaylistEntryId{"gone"}};
    EXPECT_FALSE(transfer.move_to(source, target, gone).succeeded());
}

TEST(PlaylistTransfer, RequiresAnExplicitPersistencePolicyAtConstruction)
{
    wiola::library::InMemoryRepository repository;
    wiola::app::PlaylistDestination destination;
    EXPECT_THROW((wiola::app::PlaylistTransfer{repository, destination, {}}),
        std::invalid_argument);
}

TEST(PlaylistTransfer, AChangedDestinationDoesNotRetargetAnAlreadyAdmittedTransfer)
{
    using namespace wiola;
    library::InMemoryRepository repository;
    app::PlaylistDestination destination;
    const auto original = repository.create_playlist("Original");
    const auto replacement = repository.create_playlist("Replacement");
    ASSERT_TRUE(destination.set(original));
    app::PlaylistTransfer transfer{repository, destination, [&](const auto&) {
                                       static_cast<void>(destination.set(replacement));
                                       return true;
                                   }};

    const auto result = transfer.add_to_destination({
        {.media = {"media"}, .location = {}}
    });

    EXPECT_TRUE(result.succeeded());
    EXPECT_EQ(destination.id(), replacement);
    EXPECT_EQ(repository.playlist(original)->entries.size(), 1U);
    EXPECT_TRUE(repository.playlist(replacement)->entries.empty());
}

TEST(PlaylistTransfer, AnAcceptedMoveKeepsItsReceiptAfterDisposingTheCommandFacade)
{
    using namespace wiola;
    library::InMemoryRepository repository;
    app::PlaylistDestination destination;
    const auto source = repository.create_playlist("Source");
    const auto target = repository.create_playlist("Target");
    library::TrackRecord record;
    record.id = {"media"};
    ASSERT_TRUE(repository.put_track(record));
    const auto entry = repository.append_track(source, record.id);
    ASSERT_TRUE(entry);
    std::unique_ptr<app::PlaylistTransfer> transfer;
    transfer = std::make_unique<app::PlaylistTransfer>(repository, destination, [&](const auto&) {
        const auto revision = repository.staged_delta()->changes.revision;
        transfer.reset();
        return library::CommitResult::accepted(revision,
            std::make_shared<library::CommitReceipt>(revision));
    });
    const std::vector entries{*entry};

    const auto result = transfer->move_to(source, target, entries);

    EXPECT_FALSE(transfer);
    EXPECT_TRUE(result.succeeded());
    ASSERT_TRUE(result.commit.receipt);
    EXPECT_FALSE(result.commit.receipt->durable());
    result.commit.receipt->notify(true);
    EXPECT_TRUE(result.commit.receipt->durable());
    EXPECT_TRUE(repository.playlist(source)->entries.empty());
    EXPECT_EQ(repository.playlist(target)->entries.size(), 1U);
}

} // namespace
