/**
 * @file
 * @brief Unit tests for reading metadata off the thread that queued it.
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

#include <engine/load/tag_reader.hpp>

#include <fakes/decoder.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

using wiola::codec::Tags;
using wiola::engine::TagReader;
using wiola::testing::Gate;

std::vector<TagReader::Reading> wait_for(TagReader& reader)
{
    while (reader.busy())
        std::this_thread::yield();

    return reader.take();
}

} // namespace

TEST(TagReader, HasNothingBeforeItIsAsked)
{
    TagReader reader;

    EXPECT_FALSE(reader.busy());
    EXPECT_TRUE(reader.take().empty());
}

TEST(TagReader, ReadsWithTheSuppliedCollaborator)
{
    TagReader reader{[](const std::filesystem::path& path) {
        Tags tags;
        tags.track.artist = path.stem().string();
        return tags;
    }};

    reader.start({
        {.id = wiola::engine::QueueEntryId{1}, .path = "first.wav" },
        {.id = wiola::engine::QueueEntryId{2}, .path = "second.wav"}
    });

    const std::vector<TagReader::Reading> readings{wait_for(reader)};

    ASSERT_EQ(readings.size(), 2U);
    EXPECT_EQ(readings[0].id, wiola::engine::QueueEntryId{1});
    EXPECT_EQ(readings[0].path, std::filesystem::path{"first.wav"});
    EXPECT_EQ(readings[0].tags.track.artist, "first");
    EXPECT_EQ(readings[1].id, wiola::engine::QueueEntryId{2});
    EXPECT_EQ(readings[1].path, std::filesystem::path{"second.wav"});
    EXPECT_EQ(readings[1].tags.track.artist, "second");
    EXPECT_TRUE(reader.take().empty());
}

TEST(TagReader, HoldsAReadUntilItsCollaboratorReturns)
{
    Gate reading;
    TagReader reader{[&reading](const std::filesystem::path&) {
        reading.wait();
        Tags tags;
        tags.track.title = "released";
        return tags;
    }};

    reader.start({
        {.id = wiola::engine::QueueEntryId{7}, .path = "track.wav"}
    });
    reading.wait_until_entered();

    EXPECT_TRUE(reader.busy());
    EXPECT_TRUE(reader.take().empty());

    reading.release();

    const std::vector<TagReader::Reading> readings{wait_for(reader)};
    ASSERT_EQ(readings.size(), 1U);
    EXPECT_EQ(readings.front().tags.track.title, "released");
}

TEST(TagReader, IgnoresAnEmptyBatch)
{
    TagReader reader{[](const std::filesystem::path&) { return Tags{}; }};

    reader.start({});

    EXPECT_FALSE(reader.busy());
    EXPECT_TRUE(reader.take().empty());
}

TEST(TagReader, TurnsAWorkerExceptionIntoAFailedReading)
{
    TagReader reader{[](const std::filesystem::path& path) -> Tags {
        if (path == "broken.wav")
            throw std::runtime_error{"broken metadata"};
        return Tags{};
    }};

    reader.start({
        {.id = wiola::engine::QueueEntryId{1}, .path = "broken.wav" },
        {.id = wiola::engine::QueueEntryId{2}, .path = "working.wav"}
    });

    const auto readings{wait_for(reader)};
    ASSERT_EQ(readings.size(), 2U);
    EXPECT_TRUE(readings[0].failed);
    EXPECT_FALSE(readings[1].failed);
}

TEST(TagReader, DropsAnArtworkPayloadLargerThanTheCompletedByteBudget)
{
    TagReader reader{[](const std::filesystem::path&) {
                         Tags tags;
                         tags.artwork.data.resize(5);
                         return tags;
                     },
        1, 1, 4};

    reader.start({
        {.id = wiola::engine::QueueEntryId{9}, .path = "large-cover.wav"}
    });
    const auto readings{wait_for(reader)};

    ASSERT_EQ(readings.size(), 1U);
    EXPECT_TRUE(readings.front().tags.artwork.empty());
}
