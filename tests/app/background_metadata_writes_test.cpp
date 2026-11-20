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

#include <app/background_metadata_writes.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
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

    wiola::media::Result<std::unique_ptr<wiola::codec::Decoder>>
    create_decoder(const wiola::media::OperationContext&) override
    {
        return std::unique_ptr<wiola::codec::Decoder>{};
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
            .detail = "temporary output"};
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
    open(const std::filesystem::path&, const wiola::media::OperationContext&) override
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

wiola::media::MetadataWriteRequest edit(std::string title)
{
    wiola::media::MetadataWriteRequest request;
    request.overrides.title = std::move(title);
    request.expected = {.metadata_revision = 2, .content_revision = 3, .fingerprint = "abc"};

    return request;
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
    wiola::app::BackgroundMetadataWrites writes{std::make_shared<FakeProvider>(state), repository};

    std::mutex mutex;
    std::vector<wiola::app::BackgroundMetadataWriteResult> outcomes;
    auto observe = [&](wiola::app::BackgroundMetadataWriteResult result) {
        std::lock_guard lock{mutex};
        outcomes.push_back(std::move(result));
    };

    ASSERT_TRUE(writes.enqueue(media, "track.mp3", edit("first"), observe));
    ASSERT_TRUE(eventually([&] {
        std::lock_guard lock{state->mutex};
        return state->writes.size() == 1;
    }));

    ASSERT_TRUE(writes.enqueue(media, "track.mp3", edit("second"), observe));
    ASSERT_TRUE(writes.enqueue(media, "track.mp3", edit("latest"), observe));

    {
        std::lock_guard lock{state->mutex};
        state->release_first = true;
    }
    state->changed.notify_all();

    ASSERT_TRUE(eventually([&] {
        std::lock_guard lock{mutex};
        return outcomes.size() == 2;
    }));

    EXPECT_EQ(state->writes.size(), 2U);
    EXPECT_EQ(state->writes.back().overrides.title, "latest");
    EXPECT_EQ(repository.track(media)->overrides.artist, "Keep user layer");
    EXPECT_EQ(outcomes.back().media, media);
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
    wiola::app::BackgroundMetadataWrites writes{std::make_shared<FakeProvider>(state), repository};

    wiola::app::BackgroundMetadataWriteResult result;
    ASSERT_TRUE(writes.enqueue(media, "track.mp3", edit("title"),
        [&](wiola::app::BackgroundMetadataWriteResult outcome) { result = std::move(outcome); }));

    EXPECT_EQ(result.outcome.state, wiola::media::MetadataWriteState::failed);
    EXPECT_EQ(result.outcome.error, wiola::media::MetadataWriteError::conflicted);
    EXPECT_FALSE(result.outcome.source_modified);
    EXPECT_TRUE(state->writes.empty());
}

} // namespace
