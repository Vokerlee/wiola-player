/**
 * @file
 * @brief Captured metadata edit targets, expected state, mixed fields and draft.
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

#include <algorithm>
#include <exception>
#include <media/local/fingerprint.hpp>
#include <media/local/resource.hpp>
#include <stdexcept>

namespace wiola::app {

BackgroundMetadataWrites::BackgroundMetadataWrites(std::shared_ptr<media::MediaProvider> provider)
    : provider_{std::move(provider)}
    , worker_{[this] { run(); }}
{
}

BackgroundMetadataWrites::~BackgroundMetadataWrites()
{
    try {
        close();
    } catch (...) {
    }
}

void BackgroundMetadataWrites::cancel(Pending pending)
{
    pending.job.cancellation->request_stop();
    if (pending.observer)
        pending.observer({
            .job = std::move(pending.job),
            .stage = MetadataWriteStage::cancelled,
            .outcome = {.state = media::MetadataWriteState::failed,
                        .error = media::MetadataWriteError::cancelled,
                        .source_modified = false,
                        .detail = "write cancelled or superseded"},
            .output = {}
        });
}

void BackgroundMetadataWrites::stop()
{
    if (worker_.get_id() == std::this_thread::get_id())
        throw std::logic_error{"metadata write worker cannot join itself"};
    close();
}

void BackgroundMetadataWrites::close()
{
    std::unique_ptr<std::deque<Pending>> cancelled;
    std::shared_ptr<std::stop_source> active;
    {
        std::lock_guard lock{mutex_};
        if (stopping_)
            return;
        stopping_ = true;
        cancelled = std::move(jobs_);
        active = active_;
    }
    available_.notify_all();
    if (active)
        active->request_stop();
    if (worker_.joinable())
        worker_.join();

    auto failure = delivery_failure_;
    for (auto& pending : *cancelled) {
        try {
            cancel(std::move(pending));
        } catch (...) {
            if (!failure)
                failure = std::current_exception();
        }
    }
    if (failure)
        std::rethrow_exception(failure);
}

std::optional<MetadataWriteOperationId> BackgroundMetadataWrites::enqueue(MetadataWriteJob job,
    Observer observer, Observer progress)
{
    if (!job.media.valid() || job.source.empty() || !observer || !job.cancellation)
        return {};
    std::optional<Pending> superseded;
    MetadataWriteOperationId id;
    {
        std::lock_guard lock{mutex_};
        if (stopping_)
            return {};
        const auto previous = std::ranges::find_if(*jobs_,
            [&](const auto& item) { return item.job.media == job.media; });
        if (previous == jobs_->end() && jobs_->size() >= 256)
            return {};
        id = job.operation.valid() ? job.operation : MetadataWriteOperationId{++next_operation_};
        job.operation = id;
        if (previous != jobs_->end()) {
            superseded = std::move(*previous);
            *previous = {std::move(job), std::move(observer), std::move(progress)};
        } else
            jobs_->push_back({std::move(job), std::move(observer), std::move(progress)});
    }
    if (superseded)
        cancel(std::move(*superseded));
    available_.notify_one();
    return id;
}

MetadataWriteResult BackgroundMetadataWrites::stage(const Pending& pending)
{
    MetadataWriteResult result{.job = pending.job,
        .stage = MetadataWriteStage::failed,
        .outcome = {},
        .output = {}};
    try {
        const auto& job = pending.job;
        const auto fingerprint = media::fingerprint_media(job.source);
        if (fingerprint)
            result.source_fingerprint = *fingerprint;
        if (job.context().stop_reason() != media::StopReason::none)
            result.stage = MetadataWriteStage::cancelled;
        else if (!job.expected_source.fingerprint.empty() &&
            media::fingerprint_media(job.source) !=
                std::optional{job.expected_source.fingerprint}) {
            result.stage = MetadataWriteStage::conflicted;
            result.outcome.error = media::MetadataWriteError::conflicted;
            result.outcome.detail = "source changed before staging";
        } else if (!fingerprint) {
            result.outcome.error = media::MetadataWriteError::io;
            result.outcome.detail = "could not fingerprint source before staging";
        } else {
            if (pending.progress)
                pending.progress({.job = job,
                    .stage = MetadataWriteStage::remuxing,
                    .outcome = {},
                    .output = {},
                    .source_fingerprint = *fingerprint});
            auto patch = library::file_metadata_patch(job.patch);
            auto asset = provider_->open(wiola::media::local_resource(job.source), job.context());
            if (!patch) {
                result.outcome.error = media::MetadataWriteError::invalid_edit;
                result.outcome.detail = patch.error().detail;
            } else if (!asset) {
                result.outcome.error = media::MetadataWriteError::io;
                result.outcome.detail = asset.error().detail;
            } else {
                // Application override revisions are deliberately not source-file revisions.
                auto expected = job.expected_source;
                expected.metadata_revision.reset();
                result.job.capabilities = (*asset)->metadata_write_capabilities();
                result.outcome =
                    (*asset)->write_metadata({.overrides = *patch, .expected = expected},
                        job.context());
                if (result.outcome.state == media::MetadataWriteState::written &&
                    !result.outcome.source_modified) {
                    const auto& path = result.outcome.staged_output;
                    if (!path.empty() && path.lexically_normal() != job.source.lexically_normal()) {
                        result.output = std::make_shared<StagedMetadataOutput>(path);
                        result.stage = job.context().stop_reason() == media::StopReason::none
                            ? MetadataWriteStage::ready
                            : MetadataWriteStage::cancelled;
                    } else
                        result.outcome.error = media::MetadataWriteError::invalid_output;
                } else if (result.outcome.error == media::MetadataWriteError::cancelled)
                    result.stage = MetadataWriteStage::cancelled;
            }
        }
    } catch (const std::exception& error) {
        result.outcome = {.state = media::MetadataWriteState::failed,
            .error = media::MetadataWriteError::io,
            .source_modified = false,
            .detail = error.what()};
    } catch (...) {
        result.outcome = {.state = media::MetadataWriteState::failed,
            .error = media::MetadataWriteError::io,
            .source_modified = false,
            .detail = "driver failure"};
    }
    if (result.stage != MetadataWriteStage::ready) {
        result.outcome.state = media::MetadataWriteState::failed;
        if (result.stage == MetadataWriteStage::cancelled)
            result.outcome.error = media::MetadataWriteError::cancelled;
        result.output.reset();
    }
    return result;
}

void BackgroundMetadataWrites::run()
{
    for (;;) {
        Pending pending;
        {
            std::unique_lock lock{mutex_};
            available_.wait(lock, [this] { return stopping_ || !jobs_->empty(); });
            if (stopping_)
                return;
            pending = std::move(jobs_->front());
            jobs_->pop_front();
            active_ = pending.job.cancellation;
        }
        try {
            auto result = stage(pending);
            {
                std::lock_guard lock{mutex_};
                active_.reset();
            }
            pending.observer(std::move(result));
        } catch (...) {
            std::lock_guard lock{mutex_};
            active_.reset();
            if (!delivery_failure_)
                delivery_failure_ = std::current_exception();
        }
    }
}

} // namespace wiola::app
