/**
 * @file
 * @brief Metadata coordination, capacity, cancellation, and coalescing tests.
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

#include <fakes/resource.hpp>
#include <metadata/metadata_coordinator.hpp>

#include <fakes/decoder.hpp>
#include <fakes/provider.hpp>
#include <queue/playlist.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using wiola::engine::MetadataCoordinator;
using wiola::engine::Playlist;
using wiola::engine::QueueEntry;
using wiola::testing::Gate;

std::vector<MetadataCoordinator::Reading> wait_for(MetadataCoordinator& coordinator)
{
    std::vector<MetadataCoordinator::Reading> readings;

    while (coordinator.busy()) {
        for (auto& reading : coordinator.take())
            readings.push_back(std::move(reading));

        std::this_thread::yield();
    }

    for (auto& reading : coordinator.take())
        readings.push_back(std::move(reading));

    return readings;
}

class ControlledMetadataWorker final : public wiola::engine::MetadataWorker {
public:
    std::size_t submit(std::span<Request> requests) override
    {
        const auto accepted{requests.first(std::min(requests.size(), acceptance_limit))};
        for (auto& request : accepted) {
            submitted.push_back(request);
            pending.push_back(std::move(request));
        }
        return accepted.size();
    }

    void cancel_before(std::uint64_t generation) override
    {
        std::erase_if(pending,
            [generation](const auto& request) { return request.generation < generation; });
    }

    std::vector<Reading> take() override { return std::exchange(ready, {}); }

    bool busy() const noexcept override { return !pending.empty(); }

    void deliver(const Request& request, wiola::media::Metadata metadata = {})
    {
        std::erase_if(pending, [&](const auto& current) {
            return current.generation == request.generation && current.id == request.id &&
                current.resource == request.resource;
        });
        ready.push_back({.generation = request.generation,
            .id = request.id,
            .resource = request.resource,
            .result = std::move(metadata)});
    }

    std::vector<Request> submitted;
    std::vector<Request> pending;
    std::vector<Reading> ready;
    std::size_t acceptance_limit{std::numeric_limits<std::size_t>::max()};
};

TEST(MetadataCoordinator, DeferredWorkRemainsBusyWhenTheWorkerIsIdle)
{
    auto worker{std::make_unique<ControlledMetadataWorker>()};
    auto* controlled{worker.get()};
    controlled->acceptance_limit = 0;
    MetadataCoordinator coordinator{std::move(worker)};
    Playlist queue;
    queue.set({wiola::testing::resource("first.media", "first"),
        wiola::testing::resource("second.media", "second")});

    coordinator.replace(queue.tracks());
    EXPECT_TRUE(controlled->submitted.empty());
    EXPECT_FALSE(controlled->busy());
    EXPECT_TRUE(coordinator.busy());

    controlled->acceptance_limit = 1;
    EXPECT_TRUE(coordinator.take().empty());
    ASSERT_EQ(controlled->submitted.size(), 1U);
    const auto first{controlled->submitted.front()};
    controlled->deliver(first);
    EXPECT_FALSE(controlled->busy());
    EXPECT_TRUE(coordinator.busy());

    const auto accepted_first{coordinator.take()};
    ASSERT_EQ(accepted_first.size(), 1U);
    EXPECT_EQ(accepted_first[0].entries, std::vector{first.id});
    ASSERT_EQ(controlled->submitted.size(), 2U);
    const auto second{controlled->submitted.back()};
    EXPECT_NE(first.id, second.id);
    controlled->deliver(second);

    const auto accepted_second{coordinator.take()};
    ASSERT_EQ(accepted_second.size(), 1U);
    EXPECT_EQ(accepted_second[0].entries, std::vector{second.id});
    EXPECT_FALSE(coordinator.busy());
    EXPECT_TRUE(coordinator.take().empty());
}

TEST(MetadataCoordinator, RejectsANullWorkerAtConstruction)
{
    EXPECT_THROW((MetadataCoordinator{std::unique_ptr<wiola::engine::MetadataWorker>{}}),
        std::invalid_argument);
}

TEST(MetadataCoordinator, LateResultForTheSamePathCannotBindToANewerOccurrence)
{
    auto worker{std::make_unique<ControlledMetadataWorker>()};
    auto* controlled{worker.get()};
    MetadataCoordinator coordinator{std::move(worker)};
    Playlist queue;
    queue.set({wiola::testing::resource("same.wav", "same")});
    coordinator.replace(queue.tracks());
    const auto old{controlled->submitted.back()};
    queue.set({wiola::testing::resource("same.wav", "same")});
    coordinator.replace(queue.tracks());
    const auto current{controlled->submitted.back()};
    controlled->deliver(old);
    EXPECT_TRUE(coordinator.take().empty());
    EXPECT_TRUE(coordinator.busy());
    controlled->deliver(current);
    const auto accepted{coordinator.take()};
    ASSERT_EQ(accepted.size(), 1U);
    EXPECT_EQ(accepted.front().entries, std::vector{queue.current()->id()});
    EXPECT_FALSE(coordinator.busy());
}

TEST(MetadataCoordinator, WrongRequestIdentityLeavesTheRealRequestPending)
{
    auto worker{std::make_unique<ControlledMetadataWorker>()};
    auto* controlled{worker.get()};
    MetadataCoordinator coordinator{std::move(worker)};
    Playlist queue;
    queue.set({wiola::testing::resource("current.wav", "current")});
    coordinator.replace(queue.tracks());
    const auto current{controlled->submitted.back()};
    auto wrong{current};
    ++wrong.generation;
    controlled->deliver(wrong);
    wrong = current;
    wrong.id = wiola::engine::QueueEntryId{};
    controlled->deliver(wrong);
    wrong = current;
    wrong.resource = wiola::testing::resource("foreign.wav", "foreign");
    controlled->deliver(wrong);
    EXPECT_TRUE(coordinator.take().empty());
    controlled->deliver(current);
    ASSERT_EQ(coordinator.take().size(), 1U);
    controlled->deliver(current);
    EXPECT_TRUE(coordinator.take().empty());
}

TEST(MetadataCoordinator, OutOfOrderCompletionRetainsEachOccurrenceAssociation)
{
    auto worker{std::make_unique<ControlledMetadataWorker>()};
    auto* controlled{worker.get()};
    MetadataCoordinator coordinator{std::move(worker)};
    Playlist queue;
    queue.set({wiola::testing::resource("first.wav", "first"),
        wiola::testing::resource("second.wav", "second")});
    coordinator.replace(queue.tracks());
    const auto first{controlled->submitted.front()};
    const auto second{controlled->submitted.back()};
    controlled->deliver(second);
    controlled->deliver(first);
    const auto accepted{coordinator.take()};
    ASSERT_EQ(accepted.size(), 2U);
    EXPECT_EQ(accepted[0].entries, std::vector{queue.tracks()[1].id()});
    EXPECT_EQ(accepted[1].entries, std::vector{queue.tracks()[0].id()});
}

TEST(MetadataCoordinator, CoalescesDuplicatePathsWithoutMergingEntryIdentity)
{
    Playlist playlist;
    playlist.set({wiola::testing::resource("same.wav", "same"),
        wiola::testing::resource("same.wav", "same")});
    std::atomic<std::size_t> num_reads{0};
    MetadataCoordinator coordinator{wiola::testing::FakeMediaProvider::metadata_only(
        [&num_reads](const wiola::media::ResourceReference&,
            const wiola::media::OperationContext&) {
            ++num_reads;
            return wiola::media::Metadata{};
        }
    )};

    coordinator.replace(playlist.tracks());
    const auto readings{wait_for(coordinator)};

    ASSERT_EQ(readings.size(), 1U);
    EXPECT_EQ(num_reads, 1U);
    EXPECT_EQ(readings.front().entries,
        (std::vector{playlist.tracks()[0].id(), playlist.tracks()[1].id()}));
}

TEST(MetadataCoordinator, DropsAnObsoleteGenerationAfterItsPhysicalRead)
{
    Gate opening;
    Playlist old_queue;
    old_queue.set({wiola::testing::resource("old.wav", "old")});
    Playlist new_queue;
    new_queue.set({wiola::testing::resource("new.wav", "new")});
    MetadataCoordinator coordinator{wiola::testing::FakeMediaProvider::metadata_only(
        [&opening](const wiola::media::ResourceReference& resource,
            const wiola::media::OperationContext&) {
            if (resource == wiola::testing::resource("old.wav", "old"))
                opening.wait();
            return wiola::media::Metadata{};
        }
    )};

    coordinator.replace(old_queue.tracks());
    opening.wait_until_entered();
    coordinator.replace(new_queue.tracks());
    opening.release();

    const auto readings{wait_for(coordinator)};
    ASSERT_EQ(readings.size(), 1U);
    EXPECT_EQ(readings.front().resource, wiola::testing::resource("new.wav", "new"));
    EXPECT_EQ(readings.front().entries.front(), new_queue.tracks().front().id());
}

TEST(MetadataCoordinator, FeedsMoreThanTheWorkerCapacityIncrementally)
{
    std::vector<QueueEntry> tracks;
    tracks.reserve(300);
    for (std::size_t index{0}; index < 300; ++index)
        tracks.emplace_back(wiola::engine::QueueEntryId{index + 1},
            wiola::testing::resource("track-" + std::to_string(index) + ".wav", "Track"));

    std::atomic<std::size_t> num_reads{0};
    MetadataCoordinator coordinator{wiola::testing::FakeMediaProvider::metadata_only(
                                        [&num_reads](const wiola::media::ResourceReference&,
                                            const wiola::media::OperationContext&) {
                                            ++num_reads;
                                            return wiola::media::Metadata{};
                                        }
                                    ),
        3, 2};

    coordinator.replace(tracks);

    EXPECT_TRUE(coordinator.busy());

    const auto readings{wait_for(coordinator)};

    EXPECT_EQ(readings.size(), tracks.size());
    EXPECT_EQ(num_reads, tracks.size());
    EXPECT_FALSE(coordinator.busy());
}

TEST(MetadataCoordinator, CoalescesAppendedDuplicatesBeforeDeferredSubmission)
{
    Playlist playlist;
    playlist.set({wiola::testing::resource("same.wav", "same")});
    std::atomic<std::size_t> num_reads{0};
    MetadataCoordinator coordinator{wiola::testing::FakeMediaProvider::metadata_only(
                                        [&num_reads](const wiola::media::ResourceReference&,
                                            const wiola::media::OperationContext&) {
                                            ++num_reads;
                                            return wiola::media::Metadata{};
                                        }
                                    ),
        1, 1};

    coordinator.replace(playlist.tracks());
    playlist.add({wiola::testing::resource("same.wav", "same"),
        wiola::testing::resource("other.wav", "other")});
    coordinator.append(std::span<const QueueEntry>{playlist.tracks()}.last(2));

    const auto readings{wait_for(coordinator)};

    ASSERT_EQ(readings.size(), 2U);
    EXPECT_EQ(num_reads, 2U);
    EXPECT_EQ(readings.front().entries,
        (std::vector{playlist.tracks().front().id(), playlist.tracks()[1].id()}));
}

TEST(MetadataCoordinator, CancelsDeferredWorkWhenTheGenerationChanges)
{
    Gate reading;
    std::atomic<std::size_t> num_reads{0};
    MetadataCoordinator coordinator{
        wiola::testing::FakeMediaProvider::metadata_only(
            [&reading, &num_reads](const wiola::media::ResourceReference& resource,
                const wiola::media::OperationContext&) {
                ++num_reads;
                if (resource != wiola::testing::resource("new.wav", "new"))
                    reading.wait();
                return wiola::media::Metadata{};
            }
        ),
        1, 1};
    Playlist old_queue;
    old_queue.set({wiola::testing::resource("old-0.wav", "old-0"),
        wiola::testing::resource("old-1.wav", "old-1"),
        wiola::testing::resource("old-2.wav", "old-2")});
    Playlist new_queue;
    new_queue.set({wiola::testing::resource("new.wav", "new")});

    coordinator.replace(old_queue.tracks());
    reading.wait_until_entered();
    coordinator.replace(new_queue.tracks());
    reading.release();

    const auto readings{wait_for(coordinator)};

    ASSERT_EQ(readings.size(), 1U);
    EXPECT_EQ(readings.front().resource, wiola::testing::resource("new.wav", "new"));
    EXPECT_EQ(num_reads, 2U);
}

TEST(MetadataCoordinator, CancelsDeferredWorkWithoutStartingIt)
{
    Gate reading;
    std::atomic<std::size_t> num_reads{0};
    MetadataCoordinator coordinator{
        wiola::testing::FakeMediaProvider::metadata_only(
            [&reading, &num_reads](const wiola::media::ResourceReference&,
                const wiola::media::OperationContext&) {
                ++num_reads;
                reading.wait();
                return wiola::media::Metadata{};
            }
        ),
        1, 1};
    Playlist queue;
    queue.set({wiola::testing::resource("first.wav", "first"),
        wiola::testing::resource("second.wav", "second"),
        wiola::testing::resource("third.wav", "third")});

    coordinator.replace(queue.tracks());
    reading.wait_until_entered();
    coordinator.cancel();
    reading.release();

    EXPECT_TRUE(wait_for(coordinator).empty());
    EXPECT_EQ(num_reads, 1U);
    EXPECT_FALSE(coordinator.busy());
}

TEST(MetadataCoordinator, PreservesTypedFailureBesideCoalescedEntryIdentity)
{
    Playlist playlist;
    playlist.set({wiola::testing::resource("same.wav", "same"),
        wiola::testing::resource("same.wav", "same")});
    MetadataCoordinator coordinator{wiola::testing::FakeMediaProvider::metadata_only(
        [](const wiola::media::ResourceReference&, const wiola::media::OperationContext&) {
            return wiola::media::unexpected{
                wiola::media::Error{.category = wiola::media::ErrorCategory::unsupported,
                                    .detail = "unknown tags"}
            };
        }
    )};

    coordinator.replace(playlist.tracks());
    const auto readings{wait_for(coordinator)};

    ASSERT_EQ(readings.size(), 1U);
    EXPECT_EQ(readings.front().entries.size(), 2U);
    ASSERT_FALSE(readings.front().result);
    EXPECT_EQ(readings.front().result.error().category, wiola::media::ErrorCategory::unsupported);
    EXPECT_EQ(readings.front().result.error().detail, "unknown tags");
}

TEST(MetadataCoordinator, GroupsOpaqueKeysBySourceWithoutConflatingLabels)
{
    const wiola::media::ResourceReference first{"a", "same", "First label"};
    const wiola::media::ResourceReference second{"a", "same", "Second label"};
    const wiola::media::ResourceReference other{"b", "same", "First label"};
    Playlist queue;
    queue.set({first, second, other});
    auto worker{std::make_unique<ControlledMetadataWorker>()};
    auto* observed{worker.get()};
    MetadataCoordinator coordinator{std::move(worker)};
    coordinator.replace(queue.tracks());
    ASSERT_EQ(observed->submitted.size(), 2U);
    const auto requests{observed->submitted};
    for (const auto& request : requests) {
        auto relabelled{request};
        relabelled.resource = wiola::media::ResourceReference{request.resource.source(),
            request.resource.key(), "Updated display hint"};
        observed->deliver(relabelled);
    }
    const auto readings{coordinator.take()};
    ASSERT_EQ(readings.size(), 2U);
    EXPECT_EQ(readings[0].entries.size(), 2U);
    EXPECT_EQ(readings[1].entries.size(), 1U);
    EXPECT_EQ(readings[0].entries[0], queue.tracks()[0].id());
    EXPECT_EQ(readings[0].entries[1], queue.tracks()[1].id());
    EXPECT_EQ(readings[1].entries[0], queue.tracks()[2].id());
}

} // namespace
