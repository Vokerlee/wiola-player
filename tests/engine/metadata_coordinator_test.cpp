/**
 * @file
 * @brief Metadata coordination, capacity, cancellation, and coalescing tests.
 * @author Roman Glaz
 * @copyright © 2026, <vokerlee@gmail.com>
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
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

using wiola::engine::MetadataCoordinator;
using wiola::engine::Playlist;
using wiola::engine::Track;
using wiola::testing::Gate;

std::vector<MetadataCoordinator::Reading> wait_for(MetadataCoordinator& coordinator)
{
    while (coordinator.busy())
        std::this_thread::yield();

    return coordinator.take();
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

TEST(MetadataCoordinator, RejectsAnOversizedBatchBeforeAcceptingAny)
{
    Playlist playlist;
    playlist.set({Track::of("first.wav"), Track::of("second.wav")});
    MetadataCoordinator coordinator{
        [](const std::filesystem::path&) { return wiola::media::Metadata{}; }, 1, 1};

    EXPECT_THROW(coordinator.replace(playlist.tracks()), std::length_error);
    EXPECT_FALSE(coordinator.busy());
    EXPECT_TRUE(coordinator.take().empty());
}

} // namespace
