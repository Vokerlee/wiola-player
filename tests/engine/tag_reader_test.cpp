/**
 * @file
 * @brief Unit tests for reading metadata off the thread that queued it.
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

#include <engine/load/tag_reader.hpp>

#include <fakes/decoder.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using Tags = wiola::media::Metadata;
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
    static_assert(!std::is_default_constructible_v<TagReader>);
    TagReader reader{[](const std::filesystem::path&) {
        ADD_FAILURE() << "an idle tag reader must not read metadata";
        return Tags{};
    }};

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
    EXPECT_EQ(readings[0].result->track.artist, "first");
    EXPECT_EQ(readings[1].id, wiola::engine::QueueEntryId{2});
    EXPECT_EQ(readings[1].path, std::filesystem::path{"second.wav"});
    EXPECT_EQ(readings[1].result->track.artist, "second");
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
    EXPECT_EQ(readings.front().result->track.title, "released");
}

TEST(TagReader, IgnoresAnEmptyBatch)
{
    TagReader reader{[](const std::filesystem::path&) { return Tags{}; }};

    std::vector<TagReader::Request> requests;

    EXPECT_EQ(reader.start(requests), 0U);

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
    ASSERT_FALSE(readings[0].result);
    EXPECT_EQ(readings[0].result.error().category, wiola::media::ErrorCategory::unreadable);
    EXPECT_EQ(readings[0].result.error().detail, "metadata reader failed");
    EXPECT_TRUE(readings[1].result);
    EXPECT_TRUE(readings[1].result->empty());
}

TEST(TagReader, PreservesTypedFailuresAndContinuesWithTheNextRequest)
{
    using wiola::media::ErrorCategory;
    const std::vector<std::pair<ErrorCategory, std::filesystem::path>> failures{
        {ErrorCategory::unreadable,     "unreadable.wav" },
        {ErrorCategory::unsupported,    "unsupported.wav"},
        {ErrorCategory::cancelled,      "cancelled.wav"  },
        {ErrorCategory::limit_exceeded, "limited.wav"    },
        {ErrorCategory::damaged,        "damaged.wav"    },
    };
    TagReader reader{[&failures](const std::filesystem::path& path) -> wiola::media::Result<Tags> {
        const auto failure{std::ranges::find_if(failures,
            [&path](const auto& candidate) { return candidate.second == path; })};

        if (failure == failures.end())
            return Tags{};

        return wiola::media::unexpected{
            wiola::media::Error{.category = failure->first, .detail = path.stem().string()}
        };
    }};
    std::vector<TagReader::Request> requests;

    for (std::size_t index{0}; index < failures.size(); ++index)
        requests.push_back({.id = wiola::engine::QueueEntryId{index + 1},
            .path = failures[index].second});

    requests.push_back({.id = wiola::engine::QueueEntryId{6}, .path = "working.wav"});
    reader.start(std::move(requests));

    const auto readings{wait_for(reader)};
    ASSERT_EQ(readings.size(), failures.size() + 1);

    for (std::size_t index{0}; index < failures.size(); ++index) {
        ASSERT_FALSE(readings[index].result);
        EXPECT_EQ(readings[index].result.error().category, failures[index].first);
        EXPECT_EQ(readings[index].result.error().detail, failures[index].second.stem().string());
        EXPECT_EQ(readings[index].path, failures[index].second);
    }

    EXPECT_TRUE(readings.back().result);
}

TEST(TagReader, AcceptsOnlyThePrefixThatFits)
{
    Gate reading;
    TagReader reader{[&reading](const std::filesystem::path&) {
                         reading.wait();
                         return Tags{};
                     },
        1, 3};
    std::vector<TagReader::Request> requests{
        {.id = wiola::engine::QueueEntryId{1}, .path = "first.wav" },
        {.id = wiola::engine::QueueEntryId{2}, .path = "second.wav"},
        {.id = wiola::engine::QueueEntryId{3}, .path = "third.wav" },
    };

    EXPECT_EQ(reader.start(requests), 1U);
    reading.wait_until_entered();
    EXPECT_EQ(reader.start(std::span{requests}.subspan(1)), 1U);
    EXPECT_EQ(reader.start(std::span{requests}.subspan(2)), 0U);

    reading.release();
    const auto readings{wait_for(reader)};

    ASSERT_EQ(readings.size(), 2U);
    EXPECT_EQ(readings[0].path, std::filesystem::path{"first.wav"});
    EXPECT_EQ(readings[1].path, std::filesystem::path{"second.wav"});
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
    ASSERT_TRUE(readings.front().result);
    EXPECT_TRUE(readings.front().result->artwork.empty());
}

TEST(TagReader, GenerationCancellationReachesTheActiveReadOnly)
{
    Gate reading;
    std::atomic<bool> old_was_cancelled{false};
    TagReader reader{
        [&reading, &old_was_cancelled](const std::filesystem::path& path,
            const wiola::media::OperationContext& context) -> wiola::media::Result<Tags> {
            if (path == "old.wav") {
                reading.wait();
                old_was_cancelled = context.stop_reason() == wiola::media::StopReason::cancelled;
                return wiola::media::unexpected{context.error()};
            }

            return Tags{};
        }};

    reader.start({
        {.generation = 1, .id = wiola::engine::QueueEntryId{1}, .path = "old.wav"}
    });
    reading.wait_until_entered();
    reader.cancel_before(2);
    reader.start({
        {.generation = 2, .id = wiola::engine::QueueEntryId{2}, .path = "current.wav"}
    });
    reading.release();

    const auto readings{wait_for(reader)};
    ASSERT_EQ(readings.size(), 1U);
    EXPECT_EQ(readings.front().path, std::filesystem::path{"current.wav"});
    EXPECT_TRUE(readings.front().result);
    EXPECT_TRUE(old_was_cancelled);
}

TEST(TagReader, BoundsBackendDiagnosticDetail)
{
    TagReader reader{[](const std::filesystem::path&) -> wiola::media::Result<Tags> {
        return wiola::media::unexpected{
            wiola::media::Error{.category = wiola::media::ErrorCategory::damaged,
                                .detail = std::string(2048, 'x')}
        };
    }};

    reader.start({
        {.id = wiola::engine::QueueEntryId{9}, .path = "verbose.wav"}
    });
    const auto readings{wait_for(reader)};

    ASSERT_EQ(readings.size(), 1U);
    ASSERT_FALSE(readings.front().result);
    EXPECT_EQ(readings.front().result.error().detail.size(), 1024U);
}
