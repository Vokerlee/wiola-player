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

#include <loading/loader.hpp>

#include <fakes/decoder.hpp>

#include <wiola/media/audio_decoder.hpp>

#include <gtest/gtest.h>

#include <cstdlib>
#include <exception>
#include <new>

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <thread>
#include <type_traits>
#include <utility>

namespace {

using wiola::engine::LoadCompletion;
using wiola::engine::Loader;
using wiola::engine::LoadIntent;
using wiola::engine::LoadRequest;
using wiola::engine::QueueEntryId;
using wiola::media::DecoderOpenResult;
using wiola::pcm::Frames;
using wiola::testing::FakeDecoder;
using wiola::testing::Gate;

wiola::media::DecoderOpenResult success(std::size_t num_frames = 64)
{
    return wiola::media::DecoderOpenResult{std::make_unique<FakeDecoder>(Frames{num_frames})};
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

class ObservedDecoder final : public wiola::media::AudioDecoder {
public:
    explicit ObservedDecoder(std::function<void()> destroyed)
        : AudioDecoder{{}, Frames{1}}
        , destroyed_{std::move(destroyed)}
    {
    }

    ~ObservedDecoder() override { destroyed_(); }

private:
    wiola::media::Result<wiola::media::DecodeOutcome> decode(std::span<float>, Frames) override
    {
        return wiola::media::DecodeOutcome{.frames = {}, .end_of_stream = true};
    }

    wiola::media::Result<wiola::media::SeekOutcome> seek_frame(Frames target) override
    {
        return wiola::media::SeekOutcome{.target = target, .achieved = target};
    }

    std::function<void()> destroyed_;
};

} // namespace

TEST(Loader, HasNothingBeforeItIsAsked)
{
    static_assert(!std::is_default_constructible_v<Loader>);
    Loader loader{[](const std::filesystem::path&, const wiola::media::OperationContext&) {
        ADD_FAILURE() << "an idle loader must not open media";
        return success();
    }};

    EXPECT_FALSE(loader.busy());
    EXPECT_FALSE(loader.take().has_value());
}

TEST(Loader, OpensAFileAndHandsItOver)
{
    Loader loader{[](const std::filesystem::path&, const wiola::media::OperationContext&) {
        return success();
    }};

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
    Loader loader{[](const std::filesystem::path&, const wiola::media::OperationContext&) {
        return wiola::media::DecoderOpenResult{wiola::media::unexpected{
            wiola::media::Error{.category = wiola::media::ErrorCategory::unreadable,
                .detail = "missing"}}};
    }};

    loader.start(request("missing.wav"));

    const std::optional<LoadCompletion> completed{wait_for(loader)};

    ASSERT_TRUE(completed.has_value());
    ASSERT_FALSE(completed->opened.has_value());
    EXPECT_EQ(completed->opened.error().category, wiola::media::ErrorCategory::unreadable);
    EXPECT_EQ(completed->opened.error().detail, "missing");
}

TEST(Loader, HandsTheAnswerOverOnlyOnce)
{
    Loader loader{[](const std::filesystem::path&, const wiola::media::OperationContext&) {
        return success();
    }};

    loader.start(request("track.wav"));

    ASSERT_TRUE(wait_for(loader).has_value());
    EXPECT_FALSE(loader.take().has_value());
}

TEST(Loader, HoldsALoadUntilItsOpenerReturns)
{
    Gate opening;
    Loader loader{[&opening](const std::filesystem::path&, const wiola::media::OperationContext&) {
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
    Loader loader{[&first_open, &first](const std::filesystem::path& path,
                      const wiola::media::OperationContext&) {
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
    Loader loader{[](const std::filesystem::path&, const wiola::media::OperationContext&) {
        return success();
    }};

    loader.start(request("first.wav", 1));
    ASSERT_TRUE(wait_for(loader).has_value());

    loader.start(request("again.wav", 2));

    EXPECT_TRUE(wait_for(loader).has_value());
}

TEST(Loader, DoesNotCancelACompletedDecoderWhenAnotherLoadStarts)
{
    std::stop_token first_stop;
    Loader loader{[&first_stop](const std::filesystem::path& path,
                      const wiola::media::OperationContext& context) {
        if (path == "first.wav")
            first_stop = context.stop;

        return success();
    }};

    loader.start(request("first.wav", 1));
    ASSERT_TRUE(wait_for(loader).has_value());

    loader.start(request("second.wav", 2));
    ASSERT_TRUE(wait_for(loader).has_value());

    EXPECT_FALSE(first_stop.stop_requested());
}

TEST(Loader, CanBeThrownAwayWhileItIsReading)
{
    Gate opening;
    std::jthread releaser{[&opening] {
        opening.wait_until_entered();
        opening.release();
    }};

    Loader loader{[&opening](const std::filesystem::path&, const wiola::media::OperationContext&) {
        opening.wait();
        return success();
    }};

    loader.start(request("track.wav"));
    opening.wait_until_entered();
}

TEST(Loader, ReplacementCooperativelyCancelsTheActiveOpenOnly)
{
    Gate opening;
    std::atomic<bool> old_was_cancelled{false};
    Loader loader{
        [&opening, &old_was_cancelled](const std::filesystem::path& path,
            const wiola::media::OperationContext& context) -> wiola::media::DecoderOpenResult {
            if (path == "old.wav") {
                opening.wait();
                old_was_cancelled = context.stop_reason() == wiola::media::StopReason::cancelled;
                return wiola::media::unexpected{context.error()};
            }

            return success(20);
        }};

    loader.start(request("old.wav", 1));
    opening.wait_until_entered();
    loader.start(request("current.wav", 2));
    opening.release();

    const auto completed{wait_for(loader)};
    ASSERT_TRUE(completed.has_value());
    EXPECT_EQ(completed->request.generation, 2U);
    EXPECT_TRUE(completed->opened);
    EXPECT_TRUE(old_was_cancelled);
}

TEST(Loader, TurnsAWorkerExceptionIntoAnUnreadableCompletion)
{
    Loader loader{[](const std::filesystem::path& path,
                      const wiola::media::OperationContext&) -> wiola::media::DecoderOpenResult {
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

TEST(Loader, CancellationCallbacksCanObserveLoaderStateWithoutDeadlocking)
{
    Gate opening;
    std::atomic<bool> observed{false};
    Loader* current{};
    Loader loader{
        [&](const std::filesystem::path& path, const wiola::media::OperationContext& context) {
            if (path == "old.wav") {
                std::stop_callback callback{context.stop, [&] { observed = current->busy(); }};
                opening.wait();
            }
            return success();
        }};
    current = &loader;
    loader.start(request("old.wav"));
    opening.wait_until_entered();
    loader.start(request("new.wav", 2));
    EXPECT_TRUE(observed);
    opening.release();
    const auto completed{wait_for(loader)};
    ASSERT_TRUE(completed);
    EXPECT_EQ(completed->request.generation, 2U);
}

TEST(Loader, ReplacedCompletionDestroysItsDecoderOutsideTheStateMutex)
{
    std::atomic<bool> destroyed{false};
    Loader* current{};
    Loader loader{[&](const std::filesystem::path& path,
                      const wiola::media::OperationContext&) -> DecoderOpenResult {
        if (path == "old.wav")
            return DecoderOpenResult{std::make_unique<ObservedDecoder>([&] {
                static_cast<void>(current->busy());
                destroyed = true;
            })};
        return success();
    }};
    current = &loader;
    loader.start(request("old.wav"));
    while (loader.busy())
        std::this_thread::yield();
    loader.start(request("new.wav", 2));
    EXPECT_TRUE(destroyed);
    ASSERT_TRUE(wait_for(loader));
}

TEST(Loader, CancelRetiresAnUnclaimedDecoderOutsideTheStateMutex)
{
    std::atomic<bool> destroyed{false};
    Loader* current{};
    Loader loader{[&](const std::filesystem::path&,
                      const wiola::media::OperationContext&) -> DecoderOpenResult {
        return DecoderOpenResult{std::make_unique<ObservedDecoder>([&] {
            static_cast<void>(current->busy());
            destroyed = true;
        })};
    }};
    current = &loader;
    loader.start(request("old.wav"));
    while (loader.busy())
        std::this_thread::yield();
    loader.cancel();
    EXPECT_TRUE(destroyed);
    EXPECT_FALSE(loader.busy());
    EXPECT_FALSE(loader.take());
}

TEST(Loader, CancelDropsQueuedAndLateCompletionsAndAllowsReopening)
{
    Gate opening;
    std::atomic<int> calls{0};
    Loader loader{[&](const std::filesystem::path& path, const wiola::media::OperationContext&) {
        ++calls;
        if (path == "old.wav")
            opening.wait();
        return success();
    }};
    loader.start(request("old.wav"));
    opening.wait_until_entered();
    loader.start(request("queued.wav", 2));
    loader.cancel();
    opening.release();
    EXPECT_FALSE(wait_for(loader));
    EXPECT_EQ(calls, 1);
    loader.start(request("current.wav", 3));
    const auto completed{wait_for(loader)};
    ASSERT_TRUE(completed);
    EXPECT_EQ(completed->request.generation, 3U);
    EXPECT_EQ(calls, 2);
}

TEST(Loader, CancelCooperativelyStopsTheActiveOpener)
{
    Gate opening;
    std::atomic<bool> cancelled{false};
    Loader loader{[&](const std::filesystem::path&, const wiola::media::OperationContext& context) {
        std::stop_callback callback{context.stop, [&] {
                                        cancelled = true;
                                        opening.release();
                                    }};
        opening.wait();
        return success();
    }};
    loader.start(request("old.wav"));
    opening.wait_until_entered();
    loader.cancel();
    EXPECT_FALSE(wait_for(loader));
    EXPECT_TRUE(cancelled);
}

TEST(Loader, CompletedDecoderContextSurvivesLoaderDestruction)
{
    std::stop_token completed_stop;
    std::optional<LoadCompletion> installed;
    {
        Loader loader{
            [&](const std::filesystem::path&, const wiola::media::OperationContext& context) {
                completed_stop = context.stop;
                return success();
            }};
        loader.start(request("installed.wav"));
        installed = wait_for(loader);
        ASSERT_TRUE(installed);
    }
    EXPECT_FALSE(completed_stop.stop_requested());
    ASSERT_TRUE(installed->opened);
    std::array<float, 2> samples{};
    EXPECT_TRUE((*installed->opened)->read(samples));
}

#if GTEST_HAS_DEATH_TEST
TEST(Loader, AllocationFailureRemainsFatalRatherThanAnUnreadableFile)
{
    EXPECT_EXIT(
        {
            // A fatal allocation path must not invoke arbitrary atexit handlers.
            std::set_terminate([] { std::_Exit(EXIT_FAILURE); });
            Loader loader{[](
                              const std::filesystem::path&, const wiola::media::OperationContext&
                          ) -> wiola::media::DecoderOpenResult { throw std::bad_alloc{}; }};
            loader.start(request("allocation-failure.wav"));
            while (loader.busy())
                std::this_thread::yield();
            std::_Exit(EXIT_SUCCESS);
        },
        testing::ExitedWithCode(EXIT_FAILURE), ""
    );
}
#endif
