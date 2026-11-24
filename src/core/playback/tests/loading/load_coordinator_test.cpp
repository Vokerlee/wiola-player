/**
 * @file
 * @brief Unit tests for opening files off the thread that asked.
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

#include <loading/load_coordinator.hpp>

#include <fakes/decoder.hpp>
#include <gtest/gtest.h>

#include <memory>

namespace {

using wiola::engine::LoadCompletion;
using wiola::engine::LoadCoordinator;
using wiola::engine::LoadIntent;
using wiola::engine::LoadRequest;
using wiola::engine::QueueEntryId;

LoadCompletion completion(LoadRequest request)
{
    return {.request = std::move(request),
        .opened = wiola::media::DecoderOpenResult{std::make_unique<wiola::testing::FakeDecoder>()}};
}

TEST(LoadCoordinator, RequiresTheExactActiveRequestAndAcceptsItOnlyOnce)
{
    LoadCoordinator coordinator;
    const auto active{coordinator.begin(QueueEntryId{1}, "current.wav", LoadIntent::play)};
    auto wrong{active};
    ++wrong.generation;
    EXPECT_FALSE(coordinator.accept(completion(wrong)));
    wrong = active;
    wrong.entry = QueueEntryId{2};
    EXPECT_FALSE(coordinator.accept(completion(wrong)));
    wrong = active;
    wrong.path = "different.wav";
    EXPECT_FALSE(coordinator.accept(completion(wrong)));
    wrong = active;
    wrong.intent = LoadIntent::install;
    EXPECT_FALSE(coordinator.accept(completion(wrong)));
    wrong = active;
    wrong.position = wiola::units::Time{0.5};
    EXPECT_FALSE(coordinator.accept(completion(wrong)));
    EXPECT_TRUE(coordinator.accept(completion(active)));
    EXPECT_FALSE(coordinator.accept(completion(active)));
}

TEST(LoadCoordinator, NewIntentRejectsAnOlderCompletionForTheSameResource)
{
    LoadCoordinator coordinator;
    const auto old{coordinator.begin(QueueEntryId{1}, "same.wav", LoadIntent::play)};
    const auto current{coordinator.begin(QueueEntryId{1}, "same.wav", LoadIntent::play)};
    EXPECT_NE(old.generation, current.generation);
    EXPECT_FALSE(coordinator.accept(completion(old)));
    EXPECT_TRUE(coordinator.accept(completion(current)));
}

TEST(LoadCoordinator, RemovingAnotherOccurrencePreservesTheActiveRequest)
{
    LoadCoordinator coordinator;
    const auto active{coordinator.begin(QueueEntryId{1}, "same.wav", LoadIntent::play)};
    EXPECT_FALSE(coordinator.invalidate_if(QueueEntryId{2}));
    EXPECT_TRUE(coordinator.accept(completion(active)));
    EXPECT_FALSE(coordinator.invalidate_if(QueueEntryId{1}));
}

TEST(LoadCoordinator, RemovingTheActiveOccurrenceRejectsItsLateCompletion)
{
    LoadCoordinator coordinator;
    const auto old{coordinator.begin(QueueEntryId{1}, "same.wav", LoadIntent::play)};
    EXPECT_TRUE(coordinator.invalidate_if(QueueEntryId{1}));
    EXPECT_FALSE(coordinator.accept(completion(old)));
    const auto current{coordinator.begin(QueueEntryId{1}, "same.wav", LoadIntent::play)};
    EXPECT_NE(old.generation, current.generation);
    EXPECT_TRUE(coordinator.accept(completion(current)));
}

TEST(LoadCoordinator, InvalidateDropsIntentAndPermitsANewRequest)
{
    LoadCoordinator coordinator;
    const auto old{coordinator.begin(QueueEntryId{1}, "old.wav", LoadIntent::install)};
    coordinator.invalidate();
    EXPECT_FALSE(coordinator.accept(completion(old)));
    const auto current{coordinator.begin(QueueEntryId{2}, "new.wav", LoadIntent::install)};
    EXPECT_TRUE(coordinator.accept(completion(current)));
}

TEST(LoadCoordinator, ResourceInvalidationReturnsOnlyItsExactOwnedIntent)
{
    LoadCoordinator coordinator;
    EXPECT_FALSE(coordinator.pending());
    EXPECT_FALSE(coordinator.invalidate_resource("same.media"));
    const auto active{coordinator.begin(QueueEntryId{7}, "same.media", LoadIntent::play,
        wiola::units::Time{0.5})};
    EXPECT_TRUE(coordinator.pending());
    EXPECT_FALSE(coordinator.invalidate_resource("unrelated.media"));
    EXPECT_TRUE(coordinator.pending());
    const auto retired{coordinator.invalidate_resource("same.media")};
    ASSERT_TRUE(retired);
    EXPECT_EQ(*retired, active);
    EXPECT_FALSE(coordinator.pending());
    EXPECT_FALSE(coordinator.accept(completion(active)));
    const auto resumed{
        coordinator.begin(retired->entry, retired->path, retired->intent, retired->position)};
    EXPECT_NE(resumed.generation, active.generation);
    auto wrong{resumed};
    wrong.position.reset();
    EXPECT_FALSE(coordinator.accept(completion(wrong)));
    EXPECT_TRUE(coordinator.pending());
    EXPECT_TRUE(coordinator.accept(completion(resumed)));
    EXPECT_FALSE(coordinator.pending());
}

} // namespace
