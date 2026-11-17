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

#include <engine/load/metadata_coordinator.hpp>

#include <engine/queue/playlist.hpp>
#include <fakes/decoder.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using wiola::engine::MetadataCoordinator;
using wiola::engine::Playlist;
using wiola::engine::Track;
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

TEST(MetadataCoordinator, CoalescesDuplicatePathsWithoutMergingEntryIdentity)
{
    Playlist playlist;
    playlist.set({Track::of("same.wav"), Track::of("same.wav")});
    std::atomic<std::size_t> num_reads{0};
    MetadataCoordinator coordinator{[&num_reads](const std::filesystem::path&) {
        ++num_reads;
        return wiola::media::Metadata{};
    }};

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
    old_queue.set({Track::of("old.wav")});
    Playlist new_queue;
    new_queue.set({Track::of("new.wav")});
    MetadataCoordinator coordinator{[&opening](const std::filesystem::path& path) {
        if (path == "old.wav")
            opening.wait();
        return wiola::media::Metadata{};
    }};

    coordinator.replace(old_queue.tracks());
    opening.wait_until_entered();
    coordinator.replace(new_queue.tracks());
    opening.release();

    const auto readings{wait_for(coordinator)};
    ASSERT_EQ(readings.size(), 1U);
    EXPECT_EQ(readings.front().path, std::filesystem::path{"new.wav"});
    EXPECT_EQ(readings.front().entries.front(), new_queue.tracks().front().id());
}

TEST(MetadataCoordinator, FeedsMoreThanTheWorkerCapacityIncrementally)
{
    std::vector<Track> tracks;
    tracks.reserve(300);

    for (std::size_t index{0}; index < 300; ++index)
        tracks.push_back(Track::of("track-" + std::to_string(index) + ".wav"));

    std::atomic<std::size_t> num_reads{0};
    MetadataCoordinator coordinator{[&num_reads](const std::filesystem::path&) {
                                        ++num_reads;
                                        return wiola::media::Metadata{};
                                    },
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
    playlist.set({Track::of("same.wav")});
    const Track duplicate{Track::of("same.wav")};
    const Track other{Track::of("other.wav")};
    const std::vector additions{duplicate, other};
    std::atomic<std::size_t> num_reads{0};
    MetadataCoordinator coordinator{[&num_reads](const std::filesystem::path&) {
                                        ++num_reads;
                                        return wiola::media::Metadata{};
                                    },
        1, 1};

    coordinator.replace(playlist.tracks());
    coordinator.append(additions);

    const auto readings{wait_for(coordinator)};

    ASSERT_EQ(readings.size(), 2U);
    EXPECT_EQ(num_reads, 2U);
    EXPECT_EQ(readings.front().entries,
        (std::vector{playlist.tracks().front().id(), duplicate.id()}));
}

TEST(MetadataCoordinator, CancelsDeferredWorkWhenTheGenerationChanges)
{
    Gate reading;
    std::atomic<std::size_t> num_reads{0};
    MetadataCoordinator coordinator{[&reading, &num_reads](const std::filesystem::path& path) {
                                        ++num_reads;
                                        if (path != "new.wav")
                                            reading.wait();
                                        return wiola::media::Metadata{};
                                    },
        1, 1};
    const std::vector old_tracks{Track::of("old-0.wav"), Track::of("old-1.wav"),
        Track::of("old-2.wav")};
    const std::vector new_tracks{Track::of("new.wav")};

    coordinator.replace(old_tracks);
    reading.wait_until_entered();
    coordinator.replace(new_tracks);
    reading.release();

    const auto readings{wait_for(coordinator)};

    ASSERT_EQ(readings.size(), 1U);
    EXPECT_EQ(readings.front().path, std::filesystem::path{"new.wav"});
    EXPECT_EQ(num_reads, 2U);
}

TEST(MetadataCoordinator, CancelsDeferredWorkWithoutStartingIt)
{
    Gate reading;
    std::atomic<std::size_t> num_reads{0};
    MetadataCoordinator coordinator{[&reading, &num_reads](const std::filesystem::path&) {
                                        ++num_reads;
                                        reading.wait();
                                        return wiola::media::Metadata{};
                                    },
        1, 1};
    const std::vector tracks{Track::of("first.wav"), Track::of("second.wav"),
        Track::of("third.wav")};

    coordinator.replace(tracks);
    reading.wait_until_entered();
    coordinator.cancel();
    reading.release();

    EXPECT_TRUE(wait_for(coordinator).empty());
    EXPECT_EQ(num_reads, 1U);
    EXPECT_FALSE(coordinator.busy());
}

} // namespace
