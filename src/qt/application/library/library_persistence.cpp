/**
 * @file
 * @brief Synchronized payload delivery to the desktop event loop.
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

#include "library_persistence.hpp"
#include <QThread>
#include <algorithm>
#include <stdexcept>
#include <wiola/library/records.hpp>

namespace wiola::app {
namespace {

constexpr std::size_t max_pending_revisions = 64, max_pending_bytes = 32 * 1024 * 1024;

template<class Map, class Records>
void upsert(Map& map, const Records& records)
{
    for (const auto& record : records)
        map.insert_or_assign(record.id, record);
}

template<class Map, class Ids>
void remove(Map& map, const Ids& ids)
{
    for (const auto& id : ids)
        map.erase(id);
}

template<class Map>
auto values(const Map& map)
{
    std::vector<typename Map::mapped_type> result;
    result.reserve(map.size());
    for (const auto& [id, value] : map)
        result.push_back(value);
    return result;
}

} // namespace

LibraryPersistence::LibraryPersistence(QObject& owner, persistence::qt::LibraryStore& store,
    library::LibrarySnapshot initial, Observer observer)
    : delivery_{owner}
    , lifetime_{std::make_shared<Lifetime>(Lifetime{this})}
    , store_{store}
    , observer_{std::move(observer)}
    , worker_{[this, initial = std::move(initial)]() mutable { run(std::move(initial)); }}
{
}

LibraryPersistence::~LibraryPersistence()
{
    lifetime_->owner = nullptr;
    for (const auto& [revision, receipt] : receipts_)
        receipt->cancel();
    receipts_.clear();
    awaiters_.clear();
    stop();
    delivery_.discard();
}

std::size_t LibraryPersistence::retained_bytes(const library::RevisionDelta& delta)
{
    std::size_t result = sizeof(delta) +
        512 * (delta.tracks.size() + delta.playlists.size() + delta.workspaces.size());
    const auto text = [&](const auto& value) {
        result += value.size();
    };
    for (const auto& track : delta.tracks) {
        text(track.id.value);
        text(track.fingerprint);
        for (const auto& location : track.locations)
            result += location.native().size() * sizeof(std::filesystem::path::value_type);
        for (const auto& tag : track.tags) {
            text(tag.id.value);
            text(tag.key);
            text(tag.value);
            result += sizeof(tag);
        }
        for (const auto* value :
            {&track.embedded.title, &track.embedded.artist, &track.embedded.album,
                &track.embedded.genre, &track.embedded.comment, &track.embedded.composer})
            text(*value);
        for (const auto* value :
            {&track.overrides.title, &track.overrides.artist, &track.overrides.album,
                &track.overrides.genre, &track.overrides.comment, &track.overrides.composer})
            if (*value)
                text(**value);
    }
    for (const auto& playlist : delta.playlists) {
        text(playlist.id.value);
        text(playlist.name);
        for (const auto& entry : playlist.entries) {
            text(entry.id.value);
            text(entry.media.value);
            result += sizeof(entry);
        }
    }
    for (const auto& workspace : delta.workspaces) {
        text(workspace.id.value);
        result += workspace.root.native().size() * sizeof(std::filesystem::path::value_type);
    }
    for (const auto& change : delta.changes.media) {
        text(change.media.value);
        result += sizeof(change);
    }
    for (const auto* ids : {&delta.changes.playlists, &delta.changes.removed_playlists})
        for (const auto& id : *ids) {
            text(id.value);
            result += sizeof(id);
        }
    for (const auto* ids : {&delta.changes.workspaces, &delta.changes.removed_workspaces})
        for (const auto& id : *ids) {
            text(id.value);
            result += sizeof(id);
        }
    for (const auto& id : delta.changes.removed_media) {
        text(id.value);
        result += sizeof(id);
    }
    return result;
}

library::CommitResult
LibraryPersistence::accept(std::shared_ptr<const library::RevisionDelta> delta)
{
    Q_ASSERT(QThread::currentThread() == delivery_.thread());
    if (!delta)
        return false;
    const auto bytes = retained_bytes(*delta), revision = delta->changes.revision;
    const bool critical = delta->priority == library::CommitPriority::file_result;
    const auto byte_limit = max_pending_bytes + (critical ? 16 * 1024 * 1024 : 0);
    auto receipt = std::make_shared<library::CommitReceipt>(revision);
    {
        std::lock_guard lock{mutex_};
        if (stopping_ || revision != latest_ + 1 ||
            pending_.size() >= max_pending_revisions - (critical ? 0 : 1) ||
            receipts_.size() >= 4096 - (critical ? 0 : 1) || bytes > byte_limit ||
            pending_bytes_ > byte_limit - bytes)
            return false;
        pending_.push_back(std::move(delta));
        try {
            receipts_.emplace(revision, receipt);
        } catch (...) {
            pending_.pop_back();
            throw;
        }
        pending_bytes_ += bytes;
        latest_ = revision;
    }
    changed_.notify_all();
    return library::CommitResult::accepted(revision, std::move(receipt));
}

std::uint64_t LibraryPersistence::pending_revision() const
{
    std::lock_guard lock{mutex_};
    return latest_;
}

std::uint64_t LibraryPersistence::durable_revision() const
{
    std::lock_guard lock{mutex_};
    return durable_;
}

void LibraryPersistence::retry()
{
    std::lock_guard lock{mutex_};
    if (!stopping_ && (!has_durable_ || durable_ < latest_)) {
        retry_ = true;
        changed_.notify_all();
    }
}

void LibraryPersistence::when_durable(std::uint64_t revision, std::function<void(bool)> observer)
{
    Q_ASSERT(QThread::currentThread() == delivery_.thread());
    bool saved = false, unavailable = false;
    {
        std::lock_guard lock{mutex_};
        saved = has_durable_ && durable_ >= revision;
        unavailable = stopping_ || revision > latest_;
    }
    if (saved || unavailable) {
        observer(saved);
        return;
    }
    awaiters_.push_back({revision, std::move(observer)});
    retry();
}

void LibraryPersistence::run(library::LibrarySnapshot initial)
{
    std::map<library::MediaId, library::TrackRecord> tracks;
    std::map<library::PlaylistId, library::PlaylistRecord> playlists;
    std::map<library::WorkspaceId, library::WorkspaceRecord> workspaces;
    upsert(tracks, initial.tracks);
    upsert(playlists, initial.playlists);
    upsert(workspaces, initial.workspaces);
    initial = {};
    const auto endpoint = delivery_.endpoint();
    const std::weak_ptr<Lifetime> weak = lifetime_;
    for (;;) {
        std::deque<std::shared_ptr<const library::RevisionDelta>> pending;
        std::uint64_t revision;
        {
            std::unique_lock lock{mutex_};
            changed_.wait(lock, [this] { return stopping_ || retry_ || !pending_.empty(); });
            if (stopping_ && pending_.empty() && !retry_)
                return;
            pending.swap(pending_);
            pending_bytes_ = 0;
            retry_ = false;
            active_ = true;
            revision = latest_;
        }
        for (const auto& delta : pending) {
            upsert(tracks, delta->tracks);
            upsert(playlists, delta->playlists);
            upsert(workspaces, delta->workspaces);
            remove(tracks, delta->changes.removed_media);
            remove(playlists, delta->changes.removed_playlists);
            remove(workspaces, delta->changes.removed_workspaces);
        }
        Result result;
        try {
            result = store_.save({values(tracks), values(playlists), values(workspaces)});
        } catch (const std::runtime_error&) {
            result.io = persistence::qt::FileStoreError::io;
        }
        {
            std::lock_guard lock{mutex_};
            active_ = false;
            ++attempt_;
            attempted_ = revision;
            last_result_ = result;
            if (result.succeeded()) {
                durable_ = revision;
                has_durable_ = true;
            }
            // Publish acknowledgment before waking flush: draining must include this attempt.
            static_cast<void>(endpoint.post([weak, revision, result] {
                if (const auto state = weak.lock(); state && state->owner)
                    state->owner->completed(revision, result);
            }));
        }
        changed_.notify_all();
    }
}

void LibraryPersistence::completed(std::uint64_t revision, Result result)
{
    Q_ASSERT(QThread::currentThread() == delivery_.thread());
    std::vector<std::shared_ptr<library::CommitReceipt>> receipts;
    for (auto it = receipts_.begin(); it != receipts_.end() && it->first <= revision;) {
        receipts.push_back(it->second);
        if (result.succeeded())
            it = receipts_.erase(it);
        else
            ++it;
    }
    std::vector<Awaiter> ready;
    for (auto it = awaiters_.begin(); it != awaiters_.end();) {
        if (it->revision <= revision) {
            ready.push_back(std::move(*it));
            it = awaiters_.erase(it);
        } else
            ++it;
    }
    const std::weak_ptr<Lifetime> weak = lifetime_;
    for (const auto& receipt : receipts) {
        receipt->notify(result.succeeded());
        if (const auto state = weak.lock(); !state || !state->owner)
            return;
    }
    for (const auto& awaiter : ready) {
        awaiter.observer(result.succeeded());
        if (const auto state = weak.lock(); !state || !state->owner)
            return;
    }
    const auto observer = observer_;
    if (observer)
        observer(revision, result);
}

LibraryPersistence::Result LibraryPersistence::flush()
{
    Q_ASSERT(QThread::currentThread() == delivery_.thread());
    for (;;) {
        Result result;
        std::uint64_t target;
        {
            std::unique_lock lock{mutex_};
            target = latest_;
            if (stopping_)
                return last_result_;
            const auto before = attempt_;
            if (!(has_durable_ && durable_ >= target)) {
                if (!active_ && pending_.empty())
                    retry_ = true;
                changed_.notify_all();
                changed_.wait(lock, [&] {
                    return (has_durable_ && durable_ >= target) ||
                        (attempt_ > before && attempted_ >= target);
                });
            }
            result = last_result_;
        }
        delivery_.drain();
        if (!result.succeeded() || pending_revision() == target)
            return result;
    }
}

void LibraryPersistence::stop()
{
    Q_ASSERT(QThread::currentThread() == delivery_.thread());
    {
        std::lock_guard lock{mutex_};
        stopping_ = true;
    }
    changed_.notify_all();
    if (worker_.joinable())
        worker_.join();
    delivery_.drain();
}

} // namespace wiola::app
