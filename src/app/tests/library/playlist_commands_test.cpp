/**
 * @file
 * @brief Owned playlist command outcomes and transactional rollback tests.
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

#include <allocation_failure.hpp>
#include <gtest/gtest.h>
#include <memory>
#include <new>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>
#include <wiola/app/library/playlist_name.hpp>
#include <wiola/app/library/saved_playlist_commands.hpp>

namespace {

using namespace wiola;

static_assert(std::is_nothrow_move_constructible_v<app::PlaylistCreation>);

TEST(PlaylistCommands, UnchangedNamesDoNotInventAnotherCommit)
{
    library::InMemoryRepository repository;
    std::size_t writes = 0;
    app::SavedPlaylistCommands commands{repository, [&](const auto&) {
                                            ++writes;
                                            return true;
                                        }};
    const auto created = commands.create("Saved");
    ASSERT_TRUE(created.commit);

    const auto unchanged = commands.rename(created.id, " Saved ");

    EXPECT_TRUE(unchanged);
    EXPECT_EQ(unchanged.revision, 0U);
    EXPECT_EQ(writes, 1U);
    EXPECT_EQ(repository.change_revision(), created.commit.revision);
}

TEST(PlaylistCommands, CreatesRenamesCopiesOccurrencesAndRemovesUsingTheSuppliedPersistPort)
{
    library::InMemoryRepository repository;
    std::size_t writes = 0;
    app::SavedPlaylistCommands commands{repository, [&](const auto&) {
                                            ++writes;
                                            return true;
                                        }};
    library::TrackRecord track;
    track.id = {"track"};
    ASSERT_TRUE(repository.put_track(track));
    const auto created = commands.create("  Road trip  ");
    ASSERT_TRUE(created.commit);
    ASSERT_TRUE(created.id.valid());
    ASSERT_TRUE(repository.append_track(created.id, track.id));
    ASSERT_TRUE(repository.append_track(created.id, track.id));
    EXPECT_TRUE(commands.rename(created.id, " Evening "));
    const auto copied = commands.duplicate(created.id);
    ASSERT_TRUE(copied.commit);
    EXPECT_NE(copied.id, created.id);
    const auto* playlist = repository.playlist(copied.id);
    ASSERT_NE(playlist, nullptr);
    EXPECT_EQ(playlist->name, "Evening copy");
    ASSERT_EQ(playlist->entries.size(), repository.playlist(created.id)->entries.size());
    for (std::size_t i = 0; i != playlist->entries.size(); ++i) {
        EXPECT_EQ(playlist->entries[i].media, track.id);
        EXPECT_NE(playlist->entries[i].id, repository.playlist(created.id)->entries[i].id);
    }
    EXPECT_TRUE(commands.remove(created.id));
    EXPECT_EQ(repository.playlist(created.id), nullptr);
    EXPECT_NE(repository.playlist(copied.id), nullptr);
    EXPECT_EQ(writes, 4U);
}

TEST(PlaylistCommands, InvalidOperationsDoNotReuseAnEarlierOwnedCommit)
{
    library::InMemoryRepository repository;
    std::size_t writes = 0;
    app::SavedPlaylistCommands commands{repository, [&](const auto&) {
                                            ++writes;
                                            return true;
                                        }};
    const auto created = commands.create("Saved");
    ASSERT_TRUE(created.commit);
    const auto revision = created.commit.revision;
    for (const auto& name : {std::string{}, std::string{"  "},
             std::string(app::PlaylistName::max_utf16_units + 1, 'x')}) {
        const auto rejected = commands.create(name);
        EXPECT_FALSE(rejected.id.valid());
        EXPECT_FALSE(rejected.commit);
        EXPECT_EQ(rejected.commit.revision, 0U);
        EXPECT_FALSE(commands.rename(created.id, name));
    }
    const library::PlaylistId missing{"missing"};
    EXPECT_FALSE(commands.rename(missing, "Name"));
    EXPECT_FALSE(commands.duplicate(missing).commit);
    EXPECT_FALSE(commands.remove(missing));
    EXPECT_EQ(writes, 1U);
    EXPECT_TRUE(created.commit);
    EXPECT_EQ(created.commit.revision, revision);
    EXPECT_EQ(repository.playlist(created.id)->name, "Saved");
}

TEST(PlaylistCommands, FailedPersistenceRestoresRecordsAndIdentityAllocation)
{
    library::InMemoryRepository repository;
    bool saved = false;
    app::SavedPlaylistCommands commands{repository, [&](const auto&) { return saved; }};
    EXPECT_FALSE(commands.create("Rejected").commit);
    EXPECT_TRUE(repository.playlists().empty());
    saved = true;
    const auto created = commands.create("Retained");
    ASSERT_TRUE(created.commit);
    library::InMemoryRepository fresh;
    EXPECT_EQ(created.id, fresh.create_playlist("Retained"));
    saved = false;
    EXPECT_FALSE(commands.rename(created.id, "Rejected"));
    EXPECT_FALSE(commands.duplicate(created.id).commit);
    EXPECT_FALSE(commands.remove(created.id));
    ASSERT_EQ(repository.playlists().size(), 1U);
    EXPECT_EQ(repository.playlist(created.id)->name, "Retained");
}

TEST(PlaylistCommands, DuplicationValidatesTheCompleteResultNameBeforeMutation)
{
    library::InMemoryRepository repository;
    std::size_t writes = 0;
    app::SavedPlaylistCommands commands{repository, [&](const auto&) {
                                            ++writes;
                                            return true;
                                        }};
    const auto full = commands.create(std::string(app::PlaylistName::max_utf16_units, 'x'));
    ASSERT_TRUE(full.commit);
    EXPECT_FALSE(commands.duplicate(full.id).commit);
    EXPECT_EQ(writes, 1U);
    EXPECT_EQ(repository.playlists().size(), 1U);
    const std::string suffix = " copy";
    const auto fitting =
        commands.create(std::string(app::PlaylistName::max_utf16_units - suffix.size(), 'x'));
    ASSERT_TRUE(fitting.commit);
    const auto copied = commands.duplicate(fitting.id);
    ASSERT_TRUE(copied.commit);
    EXPECT_EQ(repository.playlist(copied.id)->name.size(), app::PlaylistName::max_utf16_units);
}

TEST(PlaylistCommands, AcceptedReceiptsRemainOwnedByTheirIndividualCommandResults)
{
    library::InMemoryRepository repository;
    app::SavedPlaylistCommands commands{repository, [&](const auto&) {
                                            const auto revision =
                                                repository.staged_delta()->changes.revision;
                                            return library::CommitResult::accepted(revision,
                                                std::make_shared<library::CommitReceipt>(revision));
                                        }};
    const auto first = commands.create("First");
    const auto second = commands.create("Second");
    ASSERT_TRUE(first.commit.receipt);
    ASSERT_TRUE(second.commit.receipt);
    EXPECT_NE(first.commit.receipt, second.commit.receipt);
    EXPECT_LT(first.commit.revision, second.commit.revision);
    EXPECT_FALSE(commands.create(" ").commit);
    EXPECT_FALSE(first.commit.receipt->durable());
    second.commit.receipt->notify(true);
    EXPECT_TRUE(second.commit.receipt->durable());
    EXPECT_FALSE(first.commit.receipt->durable());
    first.commit.receipt->notify(true);
    EXPECT_TRUE(first.commit.receipt->durable());
}

TEST(PlaylistCommands, NestedChangeObserversKeepIndependentReturnedOutcomes)
{
    library::InMemoryRepository repository;
    app::SavedPlaylistCommands commands{repository, [](const auto&) { return true; }};
    std::optional<app::PlaylistCreation> nested;
    bool entered = false;
    auto subscription = repository.observe([&](const auto&) {
        if (!std::exchange(entered, true))
            nested = commands.create("Nested");
    });
    const auto outer = commands.create("Outer");
    ASSERT_TRUE(outer.commit);
    ASSERT_TRUE(nested);
    EXPECT_TRUE(nested->commit);
    EXPECT_LT(outer.commit.revision, nested->commit.revision);
    EXPECT_EQ(repository.playlist(outer.id)->name, "Outer");
    EXPECT_EQ(repository.playlist(nested->id)->name, "Nested");
}

TEST(PlaylistCommands, RetainsMutablePersistIdentityAcrossOperations)
{
    library::InMemoryRepository repository;
    std::vector<int> calls;
    app::SavedPlaylistCommands commands{repository, [count = 0, &calls](const auto&) mutable {
                                            calls.push_back(++count);
                                            return true;
                                        }};
    ASSERT_TRUE(commands.create("First").commit);
    ASSERT_TRUE(commands.create("Second").commit);
    EXPECT_EQ(calls, (std::vector<int>{1, 2}));
}

TEST(PlaylistCommands, PersistMayDisposeTheCommandFacadeWhileItsRepositoryRemainsAlive)
{
    library::InMemoryRepository repository;
    std::unique_ptr<app::SavedPlaylistCommands> commands;
    commands = std::make_unique<app::SavedPlaylistCommands>(repository, [&](const auto&) {
        commands.reset();
        return true;
    });

    const auto created = commands->create("Retained");

    EXPECT_FALSE(commands);
    ASSERT_TRUE(created.commit);
    EXPECT_EQ(repository.playlist(created.id)->name, "Retained");
}

TEST(PlaylistCommands, ThrowingOrUnavailablePersistRestoresTheCandidate)
{
    library::InMemoryRepository repository;
    app::SavedPlaylistCommands unavailable{repository, {}};
    EXPECT_THROW(static_cast<void>(unavailable.create("Unavailable")), std::bad_function_call);
    EXPECT_TRUE(repository.playlists().empty());
    app::SavedPlaylistCommands throwing{repository,
        [](const auto&) -> library::CommitResult { throw std::runtime_error{"store failed"}; }};
    EXPECT_THROW(static_cast<void>(throwing.create("Rejected")), std::runtime_error);
    EXPECT_TRUE(repository.playlists().empty());
    EXPECT_EQ(repository.change_revision(), 0U);
}

#if WIOLA_TEST_ALLOCATOR_REPLACEMENT
TEST(PlaylistCommands, EveryPreparationAllocationFailureRestoresTheRepository)
{
    bool observed_failure = false;
    for (std::size_t after = 0;; ++after) {
        library::InMemoryRepository repository;
        bool admitted = false;
        app::SavedPlaylistCommands commands{repository, [&](const auto&) {
                                                admitted = true;
                                                return true;
                                            }};
        const std::string name(app::PlaylistName::max_utf16_units / 2, 'x');
        app::PlaylistCreation result;
        bool failed = false;
        {
            const wiola::testing::AllocationFailure failure{after};
            try {
                result = commands.create(name);
            } catch (const std::bad_alloc&) {
                failed = true;
            }
        }
        if (failed) {
            observed_failure = true;
            if (!admitted) {
                EXPECT_TRUE(repository.playlists().empty());
                EXPECT_EQ(repository.change_revision(), 0U);
                result = commands.create(name);
            }
        }
        if (!failed)
            EXPECT_TRUE(result.commit);
        ASSERT_EQ(repository.playlists().size(), 1U);
        const auto stored = repository.playlists().front();
        library::InMemoryRepository fresh;
        EXPECT_EQ(stored.id, fresh.create_playlist(name));
        EXPECT_EQ(stored.name, name);
        if (!failed) {
            EXPECT_TRUE(observed_failure);
            break;
        }
    }
}
#endif

} // namespace
