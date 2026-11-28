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

#include <wiola/library/records.hpp>
#include <wiola/media/local/resource.hpp>

#include <QScopeGuard>
#include <QThread>

#include <exception>
#include <stdexcept>
#include <stop_token>

namespace wiola::app {
namespace {

std::shared_ptr<media::MediaProvider>
checked_provider(std::shared_ptr<media::MediaProvider> provider,
    const library::RepositoryTransaction::Persist& persist)
{
    if (!provider || !persist)
        throw std::invalid_argument{"metadata writes require media and persistence drivers"};
    return provider;
}

} // namespace

MetadataWriteService::MetadataWriteService(QObject& owner,
    std::shared_ptr<media::MediaProvider> provider, library::InMemoryRepository& repository,
    media::FileReplacement& replacement, library::RepositoryTransaction::Persist persist,
    Observer observer)
    : delivery_{owner}
    , lifetime_{*this}
    , provider_{checked_provider(std::move(provider), persist)}
    , repository_{repository}
    , replacement_{replacement}
    , persist_{std::make_shared<library::RepositoryTransaction::Persist>(std::move(persist))}
    , observer_{std::move(observer)}
    , staging_{provider_}
    , worker_{[this] { run(); }}
{
}

MetadataWriteService::~MetadataWriteService()
{
    try {
        stop();
    } catch (...) {
    }
    lifetime_.invalidate();
}

void MetadataWriteService::set_handoff(Handoff handoff)
{
    auto replacement = handoff ? std::make_shared<Handoff>(std::move(handoff)) : nullptr;
    auto retired = std::move(handoff_);

    handoff_ = std::move(replacement);
}

void MetadataWriteService::set_durability(DurabilityGate gate, std::function<void()> flush)
{
    auto next_gate = gate ? std::make_shared<DurabilityGate>(std::move(gate)) : nullptr;
    auto next_flush = flush ? std::make_shared<std::function<void()>>(std::move(flush)) : nullptr;
    auto retired_gate = std::move(durability_);
    auto retired_flush = std::move(flush_durability_);

    durability_ = std::move(next_gate);
    flush_durability_ = std::move(next_flush);
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

    const auto lifetime = lifetime_.observe();
    std::vector<MetadataWriteOperationId> accepted;

    if (phase_ != Phase::running)
        return accepted;

    const auto targets = edit.targets();
    const auto patch = edit.draft();

    for (const auto& target : targets) {
        cancel(target.media);

        if (!lifetime.get())
            return accepted;

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

        if (!lifetime.get())
            return accepted;

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

            if (!lifetime.get())
                return accepted;

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

            const auto observed = lifetime_.observe();
            static_cast<void>(delivery_.endpoint().post([observed, rejected] {
                if (auto* owner = observed.get())
                    owner->ready(rejected);
            }));
        }
    }

    return accepted;
}

void MetadataWriteService::cancel(const library::MediaId& media)
{
    std::vector<std::shared_ptr<std::stop_source>> cancellations;

    for (const auto& [id, job] : operations_)
        if (job.media == media)
            cancellations.push_back(job.cancellation);

    for (const auto& cancellation : cancellations)
        cancellation->request_stop();
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

    const auto observed = lifetime_.observe();
    static_cast<void>(delivery_.post([observed, prepared] {
        if (auto* owner = observed.get())
            owner->ready(prepared);
    }));
}

void MetadataWriteService::progress(MetadataWriteResult result)
{
    auto payload = std::make_shared<MetadataWriteResult>(std::move(result));
    const auto observed = lifetime_.observe();
    static_cast<void>(delivery_.post([observed, payload] {
        if (auto* owner = observed.get();
            owner && owner->operations_.contains(payload->job.operation))
            owner->publish(*payload);
    }));
}

void MetadataWriteService::ready(std::shared_ptr<Prepared> prepared)
{
    auto& result = prepared->result;

    if (phase_ != Phase::running || result.job.context().stop_reason() != media::StopReason::none ||
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
    const auto lifetime = lifetime_.observe();

    if (committing_ || ready_.empty())
        return;

    auto prepared = std::move(ready_.front());

    ready_.pop_front();

    auto& result = prepared->result;
    const auto* record = repository_.track(result.job.media);

    if (phase_ != Phase::running || result.job.context().stop_reason() != media::StopReason::none)
        result.stage = MetadataWriteStage::cancelled;
    else if (!record ||
        (result.job.expected_source.content_revision &&
            record->content_revision != *result.job.expected_source.content_revision) ||
        (!result.job.expected_source.fingerprint.empty() &&
            record->fingerprint != result.job.expected_source.fingerprint))
        result.stage = MetadataWriteStage::conflicted;

    if (result.stage != MetadataWriteStage::ready) {
        finish(std::move(result));

        if (lifetime.get())
            begin_commit();

        return;
    }

    if (durability_ && !prepared->intent_durable) {
        committing_ = true;

        const auto observed = lifetime_.observe();
        const auto durability = durability_;
        (*durability)(result.job.library_revision, [observed, prepared](bool durable) {
            if (auto* active = observed.get()) {
                auto& owner = *active;
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

                    if (auto* current = observed.get())
                        current->begin_commit();
                }
            }
        });

        return;
    }

    const auto handoff = handoff_;

    if (handoff)
        prepared->restore = (*handoff)(result.job.source);

    if (!lifetime.get())
        return;

    committing_ = true;
    result.stage = MetadataWriteStage::reloading;
    publish(result);

    if (!lifetime.get())
        return;

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

        const auto observed = lifetime_.observe();
        static_cast<void>(delivery_.post([observed, prepared] {
            if (auto* owner = observed.get())
                owner->completed(prepared);
        }));
    }
}

void MetadataWriteService::completed(std::shared_ptr<Prepared> prepared)
{
    const auto lifetime = lifetime_.observe();
    auto& result = prepared->result;

    if (prepared->restore)
        prepared->restore();

    if (!lifetime.get())
        return;

    if (result.outcome.source_modified && prepared->learned) {
        const auto* record = repository_.track(result.job.media);
        if (record) {
            auto updated = *record;
            updated.embedded = prepared->learned->track;
            updated.fingerprint = prepared->replacement->output_fingerprint;
            ++updated.content_revision;
            ++updated.metadata_revision;

            library::RepositoryTransaction transaction{repository_,
                [persist = persist_](const auto& records) { return (*persist)(records); },
                library::CommitPriority::file_result};
            library::CommitResult committed;

            if (repository_.put_track(std::move(updated)))
                committed = transaction.commit();

            if (!lifetime.get())
                return;

            if (!committed) {
                result.outcome.error = media::MetadataWriteError::io;
                result.outcome.detail = "File updated, but accepting its library update failed";
            } else if (!committed.durable() && durability_) {
                const auto observed = lifetime_.observe();
                const auto durability = durability_;
                (*durability)(committed.revision, [observed, prepared](bool durable) {
                    if (auto* current = observed.get())
                        current->finish_commit(prepared, durable);
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

    const auto lifetime = lifetime_.observe();

    finish(std::move(result));

    if (lifetime.get())
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
    Q_ASSERT(QThread::currentThread() == delivery_.thread());

    if (phase_ == Phase::closing || phase_ == Phase::closed)
        return;

    phase_ = Phase::closing;

    const auto lifetime = lifetime_.observe();
    const auto interrupted = qScopeGuard([lifetime] {
        if (auto* owner = lifetime.get(); owner && owner->phase_ == Phase::closing)
            owner->phase_ = Phase::retry;
    });
    std::exception_ptr failure;
    const auto remember_failure = [&] {
        if (!failure)
            failure = std::current_exception();
    };

    for (auto& [id, job] : operations_)
        job.cancellation->request_stop();

    try {
        staging_.stop();
    } catch (...) {
        remember_failure();
    }

    {
        std::lock_guard lock{mutex_};
        stopping_ = true;
    }
    changed_.notify_all();

    if (worker_.joinable())
        worker_.join();

    // Workers are quiescent before owner-thread observers can fail or close the service.
    const auto drain = [&] {
        while (lifetime.get()) {
            try {
                delivery_.drain();
                break;
            } catch (...) {
                remember_failure();
            }
        }
    };
    drain();

    if (!lifetime.get())
        return;

    while (!ready_.empty()) {
        auto value = std::move(ready_.front());

        ready_.pop_front();
        value->result.stage = MetadataWriteStage::cancelled;

        try {
            finish(std::move(value->result));
        } catch (...) {
            remember_failure();
        }

        if (!lifetime.get())
            return;
    }

    drain();

    if (!lifetime.get())
        return;

    try {
        const auto flush = flush_durability_;
        if (flush)
            (*flush)();
    } catch (...) {
        remember_failure();
    }

    if (!lifetime.get())
        return;

    if (failure)
        std::rethrow_exception(failure);

    phase_ = Phase::closed;
}

} // namespace wiola::app
