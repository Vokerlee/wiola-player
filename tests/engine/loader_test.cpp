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

#include <engine/load/loader.hpp>

#include <fakes/decoder.hpp>

#include <codec/decode/open.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>

namespace {

using wiola::codec::Opened;
using wiola::engine::LoadCompletion;
using wiola::engine::Loader;
using wiola::engine::LoadIntent;
using wiola::engine::LoadRequest;
using wiola::engine::QueueEntryId;
using wiola::pcm::Frames;
using wiola::testing::FakeDecoder;
using wiola::testing::Gate;

Opened success(std::size_t num_frames = 64)
{
    return Opened{std::make_unique<FakeDecoder>(Frames{num_frames})};
}

LoadRequest request(std::filesystem::path path, std::uint64_t generation = 1)
{
    return LoadRequest{.generation = generation,
        .entry = QueueEntryId{generation},
        .path = std::move(path),
        .intent = LoadIntent::install};
}

std::optional<LoadCompletion> wait_for(Loader& loader)
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

    loader.start(request("track.wav", 7));

    const std::optional<LoadCompletion> completed{wait_for(loader)};

    ASSERT_TRUE(completed.has_value());
    EXPECT_EQ(completed->request.generation, 7U);
    EXPECT_EQ(completed->request.entry, QueueEntryId{7});
    ASSERT_TRUE(completed->opened.has_value());
    ASSERT_NE(*completed->opened, nullptr);
    EXPECT_EQ((*completed->opened)->num_frames(), Frames{64});
}

TEST(Loader, SaysWhyAFileWouldNotOpen)
{
    Loader loader{[](const std::filesystem::path&) {
        return Opened{wiola::media::unexpected{
            wiola::media::Error{.category = wiola::media::ErrorCategory::unreadable,
                .detail = "missing"}}};
    }};

    loader.start(request("missing.wav"));

    const std::optional<LoadCompletion> completed{wait_for(loader)};

    ASSERT_TRUE(completed.has_value());
    ASSERT_FALSE(completed->opened.has_value());
    EXPECT_EQ(completed->opened.error().category, wiola::media::ErrorCategory::unreadable);
}

TEST(Loader, HandsTheAnswerOverOnlyOnce)
{
    Loader loader{[](const std::filesystem::path&) { return success(); }};

    loader.start(request("track.wav"));

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

    loader.start(request("track.wav"));
    opening.wait_until_entered();

    EXPECT_TRUE(loader.busy());
    EXPECT_FALSE(loader.take().has_value());

    opening.release();

    EXPECT_TRUE(wait_for(loader).has_value());
}

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

    loader.start(request(first, 1));
    first_open.wait_until_entered();
    loader.start(request("last.wav", 2));
    first_open.release();

    const std::optional<LoadCompletion> completed{wait_for(loader)};

    ASSERT_TRUE(completed.has_value());
    EXPECT_EQ(completed->request.generation, 2U);
    ASSERT_TRUE(completed->opened.has_value());
    ASSERT_NE(*completed->opened, nullptr);
    EXPECT_EQ((*completed->opened)->num_frames(), Frames{20});
    EXPECT_FALSE(loader.take().has_value());
}

TEST(Loader, TakesAnotherFileAfterOneIsDone)
{
    Loader loader{[](const std::filesystem::path&) { return success(); }};

    loader.start(request("first.wav", 1));
    ASSERT_TRUE(wait_for(loader).has_value());

    loader.start(request("again.wav", 2));

    EXPECT_TRUE(wait_for(loader).has_value());
}

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

    loader.start(request("track.wav"));
    opening.wait_until_entered();
}

TEST(Loader, TurnsAWorkerExceptionIntoAnUnreadableCompletion)
{
    Loader loader{[](const std::filesystem::path& path) -> Opened {
        if (path == "broken.wav")
            throw std::runtime_error{"broken decoder"};
        return success();
    }};

    loader.start(request("broken.wav"));
    const auto failed{wait_for(loader)};
    ASSERT_TRUE(failed.has_value());
    ASSERT_FALSE(failed->opened.has_value());
    EXPECT_EQ(failed->opened.error().category, wiola::media::ErrorCategory::unreadable);

    loader.start(request("working.wav", 2));
    const auto recovered{wait_for(loader)};
    ASSERT_TRUE(recovered.has_value());
    EXPECT_TRUE(recovered->opened.has_value());
}
