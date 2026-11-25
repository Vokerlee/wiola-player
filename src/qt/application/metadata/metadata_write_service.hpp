/**
 * @file
 * @brief Owner-thread metadata file-write orchestration with staged worker validation.
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

#pragma once
#include <app/metadata/background_metadata_writes.hpp>
#include <app/metadata/media_replacement.hpp>
#include <qt/queued_delivery.hpp>
#include <wiola/app/metadata/metadata_edit_session.hpp>
#include <wiola/library/repository_transaction.hpp>

namespace wiola::app {

/// Owner-thread orchestration. Workers use only captured jobs/provider; never the live repository.
/// The supplied replacement driver outlives stop() and is called only by the commit worker.
class MetadataWriteService final {
public:
    using Observer = std::function<void(const MetadataWriteResult&)>;
    using Restore = std::function<void()>;
    using Handoff = std::function<Restore(const std::filesystem::path&)>;
    using DurabilityGate = std::function<void(std::uint64_t, std::function<void(bool)>)>;
    MetadataWriteService(QObject& owner, std::shared_ptr<media::MediaProvider> provider,
        library::InMemoryRepository& repository, media::FileReplacement& replacement,
        library::RepositoryTransaction::Persist persist, Observer observer);
    ~MetadataWriteService();

    void set_handoff(Handoff handoff) { handoff_ = std::move(handoff); }

    void set_durability(DurabilityGate gate, std::function<void()> flush)
    {
        durability_ = std::move(gate);
        flush_durability_ = std::move(flush);
    }

    [[nodiscard]] std::vector<MetadataWriteOperationId> submit(const MetadataEditSession& edit);
    void cancel(const library::MediaId& media);
    void stop();

private:
    struct Prepared {
        MetadataWriteResult result;
        std::optional<PreparedMediaReplacement> replacement;
        std::optional<media::Metadata> learned;
        Restore restore;
        bool intent_durable{};
    };

    struct Lifetime {
        MetadataWriteService* owner;
    };

    void staged(MetadataWriteResult result);
    void progress(MetadataWriteResult result);
    void ready(std::shared_ptr<Prepared> prepared);
    void begin_commit();
    void run();
    void completed(std::shared_ptr<Prepared> prepared);
    void finish_commit(std::shared_ptr<Prepared> prepared, bool durable = true);
    void finish(MetadataWriteResult result);
    void publish(const MetadataWriteResult& result);
    QueuedDelivery delivery_;
    std::shared_ptr<Lifetime> lifetime_;
    std::shared_ptr<media::MediaProvider> provider_;
    library::InMemoryRepository& repository_;
    media::FileReplacement& replacement_;
    library::RepositoryTransaction::Persist persist_;
    Observer observer_;
    Handoff handoff_;
    DurabilityGate durability_;
    std::function<void()> flush_durability_;
    std::map<library::MediaId, std::uint64_t> generations_;
    std::map<MetadataWriteOperationId, MetadataWriteJob> operations_;
    std::deque<std::shared_ptr<Prepared>> ready_;
    bool accepting_{true}, committing_{};
    std::uint64_t next_operation_{};
    BackgroundMetadataWrites staging_;
    std::mutex mutex_;
    std::condition_variable changed_;
    std::shared_ptr<Prepared> pending_;
    bool stopping_{};
    std::jthread worker_;
};

} // namespace wiola::app
