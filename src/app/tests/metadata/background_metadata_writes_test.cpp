/**
 * @file
 * @brief Background metadata write coordination regression tests.
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

#include <app/metadata/background_metadata_writes.hpp>
#include <exception>
#include <fstream>
#include <future>
#include <iterator>
#include <media/codecs/ffmpeg/composition.hpp>
#include <media/local/provider.hpp>
#include <stdexcept>
#include <wiola/library/in_memory_repository.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace {

using namespace std::chrono_literals;

struct WriteState {
    std::mutex mutex;
    std::condition_variable changed;
    std::vector<wiola::media::MetadataWriteRequest> writes;
    bool release_first{false};
};

class FakeAsset final : public wiola::media::MediaAsset {
public:
    explicit FakeAsset(std::shared_ptr<WriteState> state)
        : state_{std::move(state)}
    {
    }

    wiola::media::Result<wiola::media::MediaInfo>
    inspect(const wiola::media::OperationContext&) override
    {
        return wiola::media::MediaInfo{};
    }

    wiola::media::Result<wiola::media::Metadata>
    read_metadata(const wiola::media::OperationContext&) override
    {
        return wiola::media::Metadata{};
    }

    wiola::media::Result<std::unique_ptr<wiola::media::AudioDecoder>>
    create_decoder(const wiola::media::OperationContext&) override
    {
        return std::unique_ptr<wiola::media::AudioDecoder>{};
    }

    wiola::media::MetadataWriteOutcome write_metadata(
        const wiola::media::MetadataWriteRequest& request, const wiola::media::OperationContext&
    ) override
    {
        std::unique_lock lock{state_->mutex};
        state_->writes.push_back(request);

        if (state_->writes.size() == 1)
            state_->changed.wait(lock, [this] { return state_->release_first; });

        state_->changed.notify_all();
        return {.state = wiola::media::MetadataWriteState::written,
            .error = wiola::media::MetadataWriteError::none,
            .source_modified = false,
            .detail = "temporary output",
            .staged_output = "temporary output"};
    }

private:
    std::shared_ptr<WriteState> state_;
};

class FakeProvider final : public wiola::media::MediaProvider {
public:
    explicit FakeProvider(std::shared_ptr<WriteState> state)
        : state_{std::move(state)}
    {
    }

    wiola::media::Result<std::unique_ptr<wiola::media::MediaAsset>>
    open(const wiola::media::ResourceReference&, const wiola::media::OperationContext&) override
    {
        return std::unique_ptr<wiola::media::MediaAsset>{std::make_unique<FakeAsset>(state_)};
    }

    std::vector<std::string> playback_extensions() const override { return {}; }

private:
    std::shared_ptr<WriteState> state_;
};

template<typename Predicate>
bool eventually(Predicate predicate)
{
    for (auto waited = 0ms; waited < 5s; waited += 5ms) {
        if (predicate())
            return true;
        std::this_thread::sleep_for(5ms);
    }

    return predicate();
}

wiola::app::MetadataWriteJob edit(std::string title)
{
    wiola::app::MetadataWriteJob job;
    job.media = {"media"};
    static const auto source = [] {
        const auto path = std::filesystem::temp_directory_path() / "wiola-fake-write-source.mp3";
        std::ofstream{path} << "fake source";
        return path;
    }();
    job.source = source;
    job.patch.title = std::move(title);
    return job;
}

TEST(BackgroundMetadataWrites, CoalescesPendingEditsWithoutReplacingRepositoryRecords)
{
    wiola::library::InMemoryRepository repository;
    const wiola::library::MediaId media{"media"};
    wiola::library::TrackRecord track;
    track.id = media;
    track.metadata_revision = 2;
    track.content_revision = 3;
    track.fingerprint = "abc";
    track.overrides.artist = "Keep user layer";
    ASSERT_TRUE(repository.put_track(track));

    const auto state = std::make_shared<WriteState>();
    wiola::app::BackgroundMetadataWrites writes{std::make_shared<FakeProvider>(state)};

    std::mutex mutex;
    std::vector<wiola::app::MetadataWriteResult> outcomes;
    auto observe = [&](wiola::app::MetadataWriteResult result) {
        std::lock_guard lock{mutex};
        outcomes.push_back(std::move(result));
    };

    ASSERT_TRUE(writes.enqueue(edit("first"), observe));
    ASSERT_TRUE(eventually([&] {
        std::lock_guard lock{state->mutex};
        return state->writes.size() == 1;
    }));

    ASSERT_TRUE(writes.enqueue(edit("second"), observe));
    ASSERT_TRUE(writes.enqueue(edit("latest"), observe));

    {
        std::lock_guard lock{state->mutex};
        state->release_first = true;
    }
    state->changed.notify_all();

    ASSERT_TRUE(eventually([&] {
        std::lock_guard lock{mutex};
        return outcomes.size() == 3;
    }));

    EXPECT_EQ(outcomes.front().stage, wiola::app::MetadataWriteStage::cancelled);
    EXPECT_NE(outcomes.front().job.operation, outcomes.back().job.operation);
    EXPECT_EQ(state->writes.size(), 2U);
    EXPECT_EQ(state->writes.back().overrides.title, "latest");
    EXPECT_EQ(repository.track(media)->overrides.artist, "Keep user layer");
    EXPECT_EQ(outcomes.back().job.media, media);
}

TEST(BackgroundMetadataWrites, RejectsStaleExpectedStateBeforeOpeningAnAsset)
{
    wiola::library::InMemoryRepository repository;
    const wiola::library::MediaId media{"media"};
    wiola::library::TrackRecord track;
    track.id = media;
    track.metadata_revision = 2;
    track.content_revision = 3;
    track.fingerprint = "different";
    ASSERT_TRUE(repository.put_track(track));

    const auto state = std::make_shared<WriteState>();
    state->release_first = true;
    wiola::app::BackgroundMetadataWrites writes{std::make_shared<FakeProvider>(state)};

    std::mutex mutex;
    std::condition_variable done;
    bool completed = false;
    wiola::app::MetadataWriteResult result;
    auto job = edit("title");
    job.expected_source.fingerprint = "different";
    ASSERT_TRUE(writes.enqueue(job, [&](auto value) {
        std::lock_guard lock{mutex};
        result = std::move(value);
        completed = true;
        done.notify_one();
    }));
    std::unique_lock lock{mutex};
    ASSERT_TRUE(done.wait_for(lock, 5s, [&] { return completed; }));
    EXPECT_EQ(result.stage, wiola::app::MetadataWriteStage::conflicted);
    EXPECT_FALSE(result.outcome.source_modified);
    EXPECT_TRUE(state->writes.empty());
}

TEST(BackgroundMetadataWrites, StagesQualifiedMediaWithoutTouchingSourceAndOwnsCleanup)
{
    const auto source = std::filesystem::temp_directory_path() / "wiola-owned-stage.mp3";
    std::error_code ignored;
    std::filesystem::remove(source, ignored);
    std::filesystem::copy_file(std::filesystem::path{WIOLA_TEST_DATA_DIR} / "tone.mp3", source);
    const auto bytes = [](const auto& path) {
        std::ifstream file{path, std::ios::binary};
        return std::vector<char>{std::istreambuf_iterator<char>{file}, {}};
    };
    const auto original = bytes(source);
    std::mutex mutex;
    std::condition_variable done;
    std::optional<wiola::app::MetadataWriteResult> result;
    wiola::app::BackgroundMetadataWrites writes{wiola::codec::ffmpeg::make_provider()};
    auto job = edit("Staged title");
    job.source = source;
    const auto operation = writes.enqueue(job, [&](auto output) {
        std::lock_guard lock{mutex};
        result = std::move(output);
        done.notify_all();
    });
    ASSERT_TRUE(operation);
    {
        std::unique_lock lock{mutex};
        ASSERT_TRUE(done.wait_for(lock, 5s, [&] { return result.has_value(); }));
    }
    writes.stop();
    ASSERT_EQ(result->stage, wiola::app::MetadataWriteStage::ready) << result->outcome.detail;
    EXPECT_EQ(result->job.operation, *operation);
    EXPECT_FALSE(result->outcome.source_modified);
    EXPECT_EQ(bytes(source), original);
    ASSERT_TRUE(result->output);
    const auto output = result->output->path();
    EXPECT_TRUE(std::filesystem::exists(output));
    result.reset();
    EXPECT_FALSE(std::filesystem::exists(output));
    std::filesystem::remove(source);
}

TEST(BackgroundMetadataWrites, CancellationHasAnIdentifiedTerminalResult)
{
    auto state = std::make_shared<WriteState>();
    state->release_first = true;
    wiola::app::BackgroundMetadataWrites writes{std::make_shared<FakeProvider>(state)};
    auto job = edit("Cancelled");
    job.cancellation->request_stop();
    std::mutex mutex;
    std::condition_variable done;
    std::optional<wiola::app::MetadataWriteResult> result;
    auto operation = writes.enqueue(job, [&](auto value) {
        std::lock_guard lock{mutex};
        result = std::move(value);
        done.notify_one();
    });
    ASSERT_TRUE(operation);
    std::unique_lock lock{mutex};
    ASSERT_TRUE(done.wait_for(lock, 5s, [&] { return result.has_value(); }));
    EXPECT_EQ(result->job.operation, *operation);
    EXPECT_EQ(result->stage, wiola::app::MetadataWriteStage::cancelled);
    EXPECT_FALSE(result->output);
    EXPECT_TRUE(state->writes.empty());
}

class ThrowOnceProvider final : public wiola::media::MediaProvider {
public:
    explicit ThrowOnceProvider(std::exception_ptr failure)
        : failure_{std::move(failure)}
    {
    }

    wiola::media::Result<std::unique_ptr<wiola::media::MediaAsset>>
    open(const wiola::media::ResourceReference&, const wiola::media::OperationContext&) override
    {
        if (auto failure = std::exchange(failure_, {}))
            std::rethrow_exception(failure);
        auto state = std::make_shared<WriteState>();
        state->release_first = true;
        return std::unique_ptr<wiola::media::MediaAsset>{std::make_unique<FakeAsset>(state)};
    }

    std::vector<std::string> playback_extensions() const override { return {}; }

private:
    std::exception_ptr failure_;
};

TEST(BackgroundMetadataWrites, DriverExceptionsProduceTerminalOutcomesAndTheWorkerRemainsUsable)
{
    struct DriverFailure { };

    for (const auto& failure : {std::make_exception_ptr(std::logic_error{"driver rejected write"}),
             std::make_exception_ptr(std::bad_alloc{}), std::make_exception_ptr(DriverFailure{})}) {
        wiola::app::BackgroundMetadataWrites writes{std::make_shared<ThrowOnceProvider>(failure)};
        std::promise<wiola::app::MetadataWriteResult> failed;
        auto first = failed.get_future();
        ASSERT_TRUE(writes.enqueue(edit("first"),
            [&](auto result) { failed.set_value(std::move(result)); }));
        ASSERT_EQ(first.wait_for(5s), std::future_status::ready);
        const auto outcome = first.get();
        EXPECT_EQ(outcome.stage, wiola::app::MetadataWriteStage::failed);
        EXPECT_EQ(outcome.outcome.error, wiola::media::MetadataWriteError::io);
        EXPECT_FALSE(outcome.outcome.source_modified);
        EXPECT_FALSE(outcome.output);

        std::promise<wiola::app::MetadataWriteResult> recovered;
        auto second = recovered.get_future();
        ASSERT_TRUE(writes.enqueue(edit("second"),
            [&](auto result) { recovered.set_value(std::move(result)); }));
        ASSERT_EQ(second.wait_for(5s), std::future_status::ready);
        EXPECT_EQ(second.get().stage, wiola::app::MetadataWriteStage::ready);
        writes.stop();
    }
}

} // namespace
