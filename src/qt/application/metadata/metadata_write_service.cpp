/**
 * @file
 * @brief Serialize staged metadata commits with owner-thread conflict checks and playback handoff.
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

#include "metadata_write_service.hpp"

#include <QThread>
#include <exception>
#include <media/local/resource.hpp>
#include <wiola/library/records.hpp>

namespace wiola::app {

MetadataWriteService::MetadataWriteService(QObject& owner,
    std::shared_ptr<media::MediaProvider> provider, library::InMemoryRepository& repository,
    media::FileReplacement& replacement, library::RepositoryTransaction::Persist persist,
    Observer observer)
    : delivery_{owner}
    , lifetime_{std::make_shared<Lifetime>(Lifetime{this})}
    , provider_{std::move(provider)}
    , repository_{repository}
    , replacement_{replacement}
    , persist_{std::move(persist)}
    , observer_{std::move(observer)}
    , staging_{provider_}
    , worker_{[this] { run(); }}
{
}

MetadataWriteService::~MetadataWriteService()
{
    stop();
    lifetime_->owner = nullptr;
}

void MetadataWriteService::publish(const MetadataWriteResult& result)
{
    Q_ASSERT(QThread::currentThread() == delivery_.thread());
    if (observer_)
        observer_(result);
}

std::vector<MetadataWriteOperationId> MetadataWriteService::submit(const MetadataEditSession& edit)
{
    Q_ASSERT(QThread::currentThread() == delivery_.thread());
    std::vector<MetadataWriteOperationId> accepted;
    if (!accepting_)
        return accepted;
    const auto targets = edit.targets();
    const auto patch = edit.draft();
    for (const auto& target : targets) {
        cancel(target.media);
        MetadataWriteJob job;
        job.operation = {++next_operation_};
        job.media = target.media;
        job.generation = ++generations_[job.media];
        job.patch = patch;
        job.library_revision = repository_.change_revision();
        job.expected_source = {.metadata_revision = {},
            .content_revision = target.expected.content_revision,
            .fingerprint = target.expected.fingerprint};
        if (const auto* current = repository_.track(job.media)) {
            job.source = library::preferred_location(*current);
            job.accepted_edit_revision = current->metadata_revision;
        }
        operations_.emplace(job.operation, job);
        accepted.push_back(job.operation);
        publish({.job = job, .stage = MetadataWriteStage::queued, .outcome = {}, .output = {}});
        if (job.context().stop_reason() != media::StopReason::none ||
            generations_[job.media] != job.generation) {
            finish({
                .job = job,
                .stage = MetadataWriteStage::cancelled,
                .outcome = {.state = media::MetadataWriteState::failed,
                            .error = media::MetadataWriteError::cancelled,
                            .source_modified = false,
                            .detail = "superseded during submission"},
                .output = {}
            });
            continue;
        }
        if (!staging_.enqueue(
                job, [this](MetadataWriteResult result) { staged(std::move(result)); },
                [this](MetadataWriteResult result) { progress(std::move(result)); }
            )) {
            auto rejected = std::make_shared<Prepared>();
            rejected->result = {
                .job = job,
                .stage = MetadataWriteStage::failed,
                .outcome = {.state = media::MetadataWriteState::failed,
                            .error = media::MetadataWriteError::io,
                            .source_modified = false,
                            .detail = "write queue rejected the request"},
                .output = {}
            };
            const std::weak_ptr<Lifetime> weak = lifetime_;
            static_cast<void>(delivery_.endpoint().post([weak, rejected] {
                if (auto state = weak.lock(); state && state->owner)
                    state->owner->ready(rejected);
            }));
        }
    }
    return accepted;
}

void MetadataWriteService::cancel(const library::MediaId& media)
{
    for (auto& [id, job] : operations_)
        if (job.media == media)
            job.cancellation->request_stop();
}

void MetadataWriteService::staged(MetadataWriteResult result)
{
    auto prepared = std::make_shared<Prepared>();
    prepared->result = std::move(result);
    auto& value = prepared->result;
    try {
        if (value.stage == MetadataWriteStage::ready && value.output) {
            progress({.job = value.job,
                .stage = MetadataWriteStage::validating,
                .outcome = {},
                .output = {}});
            auto validation = prepare_media_replacement(value.job.source, value.output->path(),
                *provider_, value.source_fingerprint, value.job.context());
            prepared->replacement = std::move(validation.prepared);
            if (!prepared->replacement) {
                value.outcome = std::move(validation.failure);
                value.stage = value.outcome.error == media::MetadataWriteError::conflicted
                    ? MetadataWriteStage::conflicted
                    : value.outcome.error == media::MetadataWriteError::cancelled
                    ? MetadataWriteStage::cancelled
                    : MetadataWriteStage::failed;
            } else {
                auto asset = provider_->open(wiola::media::local_resource(value.output->path()),
                    value.job.context());
                if (asset) {
                    auto metadata = (*asset)->read_metadata(value.job.context());
                    if (metadata)
                        prepared->learned = std::move(*metadata);
                }
                if (!prepared->learned) {
                    value.stage = MetadataWriteStage::failed;
                    value.outcome.error = media::MetadataWriteError::invalid_output;
                    value.outcome.detail = "could not read staged metadata";
                }
            }
        }
    } catch (const std::exception& error) {
        value.stage = MetadataWriteStage::failed;
        value.outcome.error = media::MetadataWriteError::io;
        value.outcome.detail = error.what();
    } catch (...) {
        value.stage = MetadataWriteStage::failed;
        value.outcome.error = media::MetadataWriteError::io;
        value.outcome.detail = "driver failure";
    }
    const std::weak_ptr<Lifetime> weak = lifetime_;
    static_cast<void>(delivery_.post([weak, prepared] {
        if (auto state = weak.lock(); state && state->owner)
            state->owner->ready(prepared);
    }));
}

void MetadataWriteService::progress(MetadataWriteResult result)
{
    auto payload = std::make_shared<MetadataWriteResult>(std::move(result));
    const std::weak_ptr<Lifetime> weak = lifetime_;
    static_cast<void>(delivery_.post([weak, payload] {
        if (auto state = weak.lock();
            state && state->owner && state->owner->operations_.contains(payload->job.operation))
            state->owner->publish(*payload);
    }));
}

void MetadataWriteService::ready(std::shared_ptr<Prepared> prepared)
{
    auto& result = prepared->result;
    if (!accepting_ || result.job.context().stop_reason() != media::StopReason::none ||
        generations_[result.job.media] != result.job.generation) {
        result.stage = MetadataWriteStage::cancelled;
        result.outcome.error = media::MetadataWriteError::cancelled;
    }
    if (result.stage != MetadataWriteStage::ready) {
        finish(std::move(result));
        return;
    }
    ready_.push_back(std::move(prepared));
    begin_commit();
}

void MetadataWriteService::begin_commit()
{
    if (committing_ || ready_.empty())
        return;
    auto prepared = std::move(ready_.front());
    ready_.pop_front();
    auto& result = prepared->result;
    const auto* record = repository_.track(result.job.media);
    if (!accepting_ || result.job.context().stop_reason() != media::StopReason::none)
        result.stage = MetadataWriteStage::cancelled;
    else if (!record ||
        (result.job.expected_source.content_revision &&
            record->content_revision != *result.job.expected_source.content_revision) ||
        (!result.job.expected_source.fingerprint.empty() &&
            record->fingerprint != result.job.expected_source.fingerprint))
        result.stage = MetadataWriteStage::conflicted;
    if (result.stage != MetadataWriteStage::ready) {
        finish(std::move(result));
        begin_commit();
        return;
    }
    if (durability_ && !prepared->intent_durable) {
        committing_ = true;
        const std::weak_ptr<Lifetime> weak = lifetime_;
        durability_(result.job.library_revision, [weak, prepared](bool durable) {
            if (const auto state = weak.lock(); state && state->owner) {
                auto& owner = *state->owner;
                owner.committing_ = false;
                if (!owner.operations_.contains(prepared->result.job.operation))
                    return;
                if (durable) {
                    prepared->intent_durable = true;
                    owner.ready_.push_front(prepared);
                    owner.begin_commit();
                } else {
                    prepared->result.stage = MetadataWriteStage::failed;
                    prepared->result.outcome = {.state = media::MetadataWriteState::failed,
                        .error = media::MetadataWriteError::io,
                        .source_modified = false,
                        .detail = "Application metadata could not be saved; the source file was "
                                  "retained"};
                    owner.finish(std::move(prepared->result));
                    owner.begin_commit();
                }
            }
        });
        return;
    }
    if (handoff_)
        prepared->restore = handoff_(result.job.source);
    committing_ = true;
    result.stage = MetadataWriteStage::reloading;
    publish(result);
    {
        std::lock_guard lock{mutex_};
        pending_ = std::move(prepared);
    }
    changed_.notify_one();
}

void MetadataWriteService::run()
{
    for (;;) {
        std::shared_ptr<Prepared> prepared;
        {
            std::unique_lock lock{mutex_};
            changed_.wait(lock, [this] { return stopping_ || pending_; });
            if (!pending_)
                return;
            prepared = std::move(pending_);
        }
        auto& result = prepared->result;
        try {
            result.outcome = commit_prepared_media_replacement(*prepared->replacement, replacement_,
                result.job.context(),
                {.before_commit = {},
                    .backup_suffix = prepared->replacement->source_fingerprint.substr(8, 16) + "." +
                        std::to_string(result.job.operation.value)});
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
        const std::weak_ptr<Lifetime> weak = lifetime_;
        static_cast<void>(delivery_.post([weak, prepared] {
            if (auto state = weak.lock(); state && state->owner)
                state->owner->completed(prepared);
        }));
    }
}

void MetadataWriteService::completed(std::shared_ptr<Prepared> prepared)
{
    auto& result = prepared->result;
    if (prepared->restore)
        prepared->restore();
    if (result.outcome.source_modified && prepared->learned) {
        const auto* record = repository_.track(result.job.media);
        if (record) {
            auto updated = *record;
            updated.embedded = prepared->learned->track;
            updated.fingerprint = prepared->replacement->output_fingerprint;
            ++updated.content_revision;
            ++updated.metadata_revision;
            library::RepositoryTransaction transaction{repository_, persist_,
                library::CommitPriority::file_result};
            library::CommitResult committed;
            if (repository_.put_track(std::move(updated)))
                committed = transaction.commit();
            if (!committed) {
                result.outcome.error = media::MetadataWriteError::io;
                result.outcome.detail = "File updated, but accepting its library update failed";
            } else if (!committed.durable() && durability_) {
                const std::weak_ptr<Lifetime> weak = lifetime_;
                durability_(committed.revision, [weak, prepared](bool durable) {
                    if (const auto state = weak.lock(); state && state->owner)
                        state->owner->finish_commit(prepared, durable);
                });
                return;
            }
        } else {
            result.outcome.error = media::MetadataWriteError::io;
            result.outcome.detail = "File updated; its library record was removed";
        }
    }
    finish_commit(std::move(prepared));
}

void MetadataWriteService::finish_commit(std::shared_ptr<Prepared> prepared, bool durable)
{
    auto& result = prepared->result;
    if (!operations_.contains(result.job.operation))
        return;
    if (!durable) {
        result.outcome.error = media::MetadataWriteError::io;
        result.outcome.detail =
            "File updated; its library changes remain in memory and need a save retry";
    }
    result.stage =
        result.outcome.source_modified && result.outcome.error == media::MetadataWriteError::none
        ? MetadataWriteStage::complete
        : result.outcome.error == media::MetadataWriteError::conflicted
        ? MetadataWriteStage::conflicted
        : result.outcome.error == media::MetadataWriteError::cancelled
        ? MetadataWriteStage::cancelled
        : MetadataWriteStage::failed;
    committing_ = false;
    finish(std::move(result));
    begin_commit();
}

void MetadataWriteService::finish(MetadataWriteResult result)
{
    operations_.erase(result.job.operation);
    result.output.reset();
    publish(result);
}

void MetadataWriteService::stop()
{
    if (!accepting_)
        return;
    accepting_ = false;
    for (auto& [id, job] : operations_)
        job.cancellation->request_stop();
    staging_.stop();
    delivery_.drain();
    while (!ready_.empty()) {
        auto value = std::move(ready_.front());
        ready_.pop_front();
        value->result.stage = MetadataWriteStage::cancelled;
        finish(std::move(value->result));
    }
    {
        std::lock_guard lock{mutex_};
        stopping_ = true;
    }
    changed_.notify_all();
    if (worker_.joinable())
        worker_.join();
    delivery_.drain();
    if (flush_durability_)
        flush_durability_();
}

} // namespace wiola::app
