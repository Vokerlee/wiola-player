/**
 * @file
 * @brief Unit tests for opening files off the thread that asked.
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

#include <engine/load/loader.hpp>

#include <fakes/decoder.hpp>

#include <codec/decode/open.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <thread>

namespace {

using wiola::codec::Opened;
using wiola::codec::OpenResult;
using wiola::engine::Loader;
using wiola::pcm::Frames;
using wiola::testing::FakeDecoder;
using wiola::testing::Gate;

Opened success(std::size_t num_frames = 64)
{
    return Opened{.decoder = std::make_unique<FakeDecoder>(Frames{num_frames}),
        .result = OpenResult::opened};
}

/// Waits for whatever was asked for, the way a caller that draws comes back for it.
std::optional<Opened> wait_for(Loader& loader)
{
    while (loader.busy())
        std::this_thread::yield();

    return loader.take();
}

} // namespace

TEST(Loader, HasNothingBeforeItIsAsked)
{
    Loader loader;

    EXPECT_FALSE(loader.busy());
    EXPECT_FALSE(loader.take().has_value());
}

TEST(Loader, OpensAFileAndHandsItOver)
{
    Loader loader{[](const std::filesystem::path&) { return success(); }};

    loader.start("track.wav");

    const std::optional<Opened> opened{wait_for(loader)};

    ASSERT_TRUE(opened.has_value());
    EXPECT_EQ(opened->result, OpenResult::opened);
    ASSERT_NE(opened->decoder, nullptr);
    EXPECT_EQ(opened->decoder->num_frames(), Frames{64});
}

TEST(Loader, SaysWhyAFileWouldNotOpen)
{
    Loader loader{[](const std::filesystem::path&) {
        return Opened{.decoder = nullptr, .result = OpenResult::unreadable};
    }};

    loader.start("missing.wav");

    const std::optional<Opened> opened{wait_for(loader)};

    ASSERT_TRUE(opened.has_value());
    EXPECT_EQ(opened->result, OpenResult::unreadable);
    EXPECT_EQ(opened->decoder, nullptr);
}

/// The answer is handed over once: a caller that comes back again gets nothing, rather than the
/// same track twice.
TEST(Loader, HandsTheAnswerOverOnlyOnce)
{
    Loader loader{[](const std::filesystem::path&) { return success(); }};

    loader.start("track.wav");

    ASSERT_TRUE(wait_for(loader).has_value());
    EXPECT_FALSE(loader.take().has_value());
}

TEST(Loader, HoldsALoadUntilItsOpenerReturns)
{
    Gate opening;
    Loader loader{[&opening](const std::filesystem::path&) {
        opening.wait();
        return success();
    }};

    loader.start("track.wav");
    opening.wait_until_entered();

    EXPECT_TRUE(loader.busy());
    EXPECT_FALSE(loader.take().has_value());

    opening.release();

    EXPECT_TRUE(wait_for(loader).has_value());
}

/// A listener who picks another file has said which one they meant, so the first answer is
/// dropped rather than played.
TEST(Loader, KeepsOnlyTheFileAskedForLast)
{
    Gate first_open;
    const std::filesystem::path first{"first.wav"};
    Loader loader{[&first_open, &first](const std::filesystem::path& path) {
        if (path == first) {
            first_open.wait();
            return success(10);
        }

        return success(20);
    }};

    loader.start(first);
    first_open.wait_until_entered();
    loader.start("last.wav");
    first_open.release();

    const std::optional<Opened> opened{wait_for(loader)};

    ASSERT_TRUE(opened.has_value());
    EXPECT_EQ(opened->result, OpenResult::opened);
    ASSERT_NE(opened->decoder, nullptr);
    EXPECT_EQ(opened->decoder->num_frames(), Frames{20});
    EXPECT_FALSE(loader.take().has_value());
}

TEST(Loader, TakesAnotherFileAfterOneIsDone)
{
    Loader loader{[](const std::filesystem::path&) { return success(); }};

    loader.start("first.wav");
    ASSERT_TRUE(wait_for(loader).has_value());

    loader.start("again.wav");

    EXPECT_TRUE(wait_for(loader).has_value());
}

/// Nothing of a file crosses back until it is whole, so a loader thrown away mid-read is not a
/// decoder half-built.
TEST(Loader, CanBeThrownAwayWhileItIsReading)
{
    Gate opening;
    std::jthread releaser{[&opening] {
        opening.wait_until_entered();
        opening.release();
    }};

    Loader loader{[&opening](const std::filesystem::path&) {
        opening.wait();
        return success();
    }};

    loader.start("track.wav");
    opening.wait_until_entered();
}
