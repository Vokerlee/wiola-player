/**
 * @file
 * @brief Identity-scoped background metadata writes with coalescing and conflict checks.
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

#include "background_metadata_writes.hpp"

#include <utility>

namespace wiola::app {

namespace {

constexpr std::size_t max_pending_writes{256};

} // namespace

BackgroundMetadataWrites::BackgroundMetadataWrites(std::shared_ptr<media::MediaProvider> provider,
    library::InMemoryRepository& repository)
    : provider_{std::move(provider)}
    , repository_{repository}
    , worker_{[this] { run(); }}
{
}

BackgroundMetadataWrites::~BackgroundMetadataWrites()
{
    {
        std::lock_guard lock{mutex_};
        stopping_ = true;
        jobs_.clear();
        pending_.clear();
    }

    available_.notify_one();
}

bool BackgroundMetadataWrites::conflicts(const Job& job) const
{
    const library::TrackRecord* track = repository_.track(job.media);
    if (track == nullptr)
        return true;

    const media::ExpectedMediaState& expected = job.request.expected;

    if (expected.metadata_revision && track->metadata_revision != *expected.metadata_revision)
        return true;

    if (expected.content_revision && track->content_revision != *expected.content_revision)
        return true;

    return !expected.fingerprint.empty() && track->fingerprint != expected.fingerprint;
}

bool BackgroundMetadataWrites::enqueue(const library::MediaId& media, std::filesystem::path path,
    media::MetadataWriteRequest request, Observer observer)
{
    if (!media.valid())
        return false;

    auto job = std::make_shared<Job>(Job{.media = media,
        .path = std::move(path),
        .request = std::move(request),
        .observer = std::move(observer)});

    if (conflicts(*job)) {
        job->observer({
            .media = job->media,
            .location = job->path,
            .outcome = {.state = media::MetadataWriteState::failed,
                        .error = media::MetadataWriteError::conflicted,
                        .source_modified = false,
                        .detail = "media changed before the metadata write"}
        });
        return true;
    }

    {
        std::lock_guard lock{mutex_};
        if (stopping_ || jobs_.size() >= max_pending_writes)
            return false;

        if (const auto pending = pending_.find(media); pending != pending_.end()) {
            pending->second->path = std::move(job->path);
            pending->second->request = std::move(job->request);
            pending->second->observer = std::move(job->observer);
        } else {
            pending_.emplace(media, job);
            jobs_.push_back(std::move(job));
        }
    }

    available_.notify_one();
    return true;
}

void BackgroundMetadataWrites::run()
{
    while (true) {
        std::shared_ptr<Job> job;

        {
            std::unique_lock lock{mutex_};
            available_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });

            if (stopping_)
                return;

            job = std::move(jobs_.front());
            jobs_.pop_front();
            pending_.erase(job->media);
        }

        auto asset = provider_->open(job->path);
        media::MetadataWriteOutcome outcome;

        if (!asset) {
            outcome = {.state = media::MetadataWriteState::failed,
                .error = media::MetadataWriteError::io,
                .source_modified = false,
                .detail = asset.error().detail};
        } else {
            outcome = (*asset)->write_metadata(job->request);
        }

        job->observer({.media = job->media, .location = job->path, .outcome = std::move(outcome)});
    }
}

} // namespace wiola::app
