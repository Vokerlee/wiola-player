/**
 * @file
 * @brief Portable bounded snapshot persistence and owner-thread acknowledgments.
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
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <wiola/library/records.hpp>

namespace wiola::app {
namespace {

LibraryPersistence::Delivery checked_delivery(LibraryPersistence::Delivery delivery)
{
    if (!delivery.post || !delivery.drain)
        throw std::invalid_argument{"library persistence requires owner delivery callbacks"};
    return delivery;
}

LibraryPersistenceLimits checked_limits(LibraryPersistenceLimits limits)
{
    if (limits.pending_revisions == 0 || limits.outstanding_receipts == 0 ||
        limits.pending_payload_bytes == 0 ||
        limits.file_result_extra_bytes >
            std::numeric_limits<std::size_t>::max() - limits.pending_payload_bytes)
        throw std::invalid_argument{"invalid library persistence capacities"};
    return limits;
}

// A conservative retained-payload budget, not allocator overhead or whole-process RSS.
// Inline string storage may be counted twice; unused owned capacity must never be omitted.
class PayloadSize final {
public:
    void add(std::size_t count, std::size_t width = 1) noexcept
    {
        const auto maximum = std::numeric_limits<std::size_t>::max();
        value_ = count > (maximum - value_) / width ? maximum : value_ + count * width;
    }

    template<class Values>
    void storage(const Values& values) noexcept
    {
        add(values.capacity(), sizeof(typename Values::value_type));
    }

    template<class Text>
    void text(const Text& text) noexcept
    {
        storage(text);
        add(1, sizeof(typename Text::value_type)); // The terminating code unit.
    }

    [[nodiscard]] std::size_t value() const noexcept { return value_; }

private:
    std::size_t value_{};
};

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

LibraryPersistence::LibraryPersistence(SnapshotWriter& store, Delivery delivery,
    library::LibrarySnapshot initial, Observer observer, LibraryPersistenceLimits limits)
    : delivery_{checked_delivery(std::move(delivery))}
    , lifetime_{std::make_shared<Lifetime>(Lifetime{this})}
    , store_{store}
    , observer_{std::move(observer)}
    , limits_{checked_limits(limits)}
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
}

std::size_t LibraryPersistence::retained_bytes(const library::RevisionDelta& delta) noexcept
{
    PayloadSize size;
    size.add(sizeof(delta));
    size.storage(delta.tracks);
    size.storage(delta.playlists);
    size.storage(delta.workspaces);
    for (const auto& track : delta.tracks) {
        size.text(track.id.value);
        size.text(track.fingerprint);
        size.storage(track.locations);
        for (const auto& location : track.locations)
            size.text(location.native());
        size.storage(track.tags);
        for (const auto& tag : track.tags) {
            size.text(tag.id.value);
            size.text(tag.key);
            size.text(tag.value);
        }
        for (const auto* value :
            {&track.embedded.title, &track.embedded.artist, &track.embedded.album,
                &track.embedded.genre, &track.embedded.comment, &track.embedded.composer})
            size.text(*value);
        for (const auto* value :
            {&track.overrides.title, &track.overrides.artist, &track.overrides.album,
                &track.overrides.genre, &track.overrides.comment, &track.overrides.composer})
            if (*value)
                size.text(**value);
    }
    for (const auto& playlist : delta.playlists) {
        size.text(playlist.id.value);
        size.text(playlist.name);
        size.storage(playlist.entries);
        for (const auto& entry : playlist.entries) {
            size.text(entry.id.value);
            size.text(entry.media.value);
        }
    }
    for (const auto& workspace : delta.workspaces) {
        size.text(workspace.id.value);
        size.text(workspace.root.native());
    }
    size.storage(delta.changes.media);
    for (const auto& change : delta.changes.media)
        size.text(change.media.value);
    for (const auto* ids : {&delta.changes.playlists, &delta.changes.removed_playlists}) {
        size.storage(*ids);
        for (const auto& id : *ids)
            size.text(id.value);
    }
    for (const auto* ids : {&delta.changes.workspaces, &delta.changes.removed_workspaces}) {
        size.storage(*ids);
        for (const auto& id : *ids)
            size.text(id.value);
    }
    size.storage(delta.changes.removed_media);
    for (const auto& id : delta.changes.removed_media)
        size.text(id.value);
    return size.value();
}

library::CommitResult
LibraryPersistence::accept(std::shared_ptr<const library::RevisionDelta> delta)
{
    check_owner();
    if (!delta)
        return false;
    const auto bytes = retained_bytes(*delta), revision = delta->changes.revision;
    const bool critical = delta->priority == library::CommitPriority::file_result;
    const auto byte_limit =
        limits_.pending_payload_bytes + (critical ? limits_.file_result_extra_bytes : 0);
    auto receipt = std::make_shared<library::CommitReceipt>(revision);
    {
        std::lock_guard lock{mutex_};
        if (stopping_ || revision != latest_ + 1 ||
            pending_.size() >= limits_.pending_revisions - (critical ? 0 : 1) ||
            receipts_.size() >= limits_.outstanding_receipts - (critical ? 0 : 1) ||
            bytes > byte_limit || pending_bytes_ > byte_limit - bytes)
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
    check_owner();
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
            result = store_.write({values(tracks), values(playlists), values(workspaces)});
        } catch (const std::runtime_error&) {
            result.error = SnapshotWriteError::io;
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
            static_cast<void>(delivery_.post([weak, revision, result] {
                if (const auto state = weak.lock(); state && state->owner)
                    state->owner->completed(revision, result);
            }));
        }
        changed_.notify_all();
    }
}

void LibraryPersistence::completed(std::uint64_t revision, Result result)
{
    check_owner();
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
    check_owner();
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
    check_owner();
    {
        std::lock_guard lock{mutex_};
        stopping_ = true;
    }
    changed_.notify_all();
    if (worker_.joinable())
        worker_.join();
    delivery_.drain();
}

void LibraryPersistence::check_owner() const
{
    if (std::this_thread::get_id() != owner_)
        throw std::logic_error{"library persistence accessed outside owner thread"};
}

} // namespace wiola::app
