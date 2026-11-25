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
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <exception>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>
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
    explicit PayloadSize(std::size_t limit) noexcept
        : limit_{limit}
    {
    }

    void add(std::size_t count, std::size_t width = 1) noexcept
    {
        if (exceeded_ || count > (limit_ - value_) / width) {
            exceeded_ = true;
            return;
        }
        value_ += count * width;
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

    [[nodiscard]] std::optional<std::size_t> value() const noexcept
    {
        if (exceeded_)
            return std::nullopt;
        return value_;
    }

private:
    const std::size_t limit_;
    std::size_t value_{};
    bool exceeded_{};
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

std::optional<std::size_t> retained_bytes(const library::RevisionDelta& delta,
    std::size_t limit) noexcept
{
    PayloadSize size{limit};
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

} // namespace

struct LibraryPersistence::State : std::enable_shared_from_this<State> {
    struct Awaiter {
        std::uint64_t revision;
        std::function<void(bool)> observer;
    };

    State(SnapshotWriter& store, Delivery delivery, Observer observer,
        LibraryPersistenceLimits limits)
        : delivery_{checked_delivery(std::move(delivery))}
        , store_{store}
        , observer_{std::move(observer)}
        , limits_{checked_limits(limits)}
    {
    }

    void check_owner() const
    {
        if (std::this_thread::get_id() != owner_)
            throw std::logic_error{"library persistence accessed outside owner thread"};
    }

    void stop_worker()
    {
        {
            std::lock_guard lock{mutex_};
            stopping_ = true;
        }
        changed_.notify_all();
        if (worker_.joinable())
            worker_.join();
    }

    void run(library::LibrarySnapshot initial);
    void completed(std::uint64_t revision, Result result);
    const std::thread::id owner_{std::this_thread::get_id()};
    const Delivery delivery_;
    SnapshotWriter& store_;
    Observer observer_;
    const LibraryPersistenceLimits limits_;
    // These observations belong exclusively to the accepting thread.
    std::map<std::uint64_t, std::shared_ptr<library::CommitReceipt>> receipts_;
    std::vector<Awaiter> awaiters_;
    // Receipt capacity is released on acknowledgment, while active notifications remain
    // owned by receipts_ so destruction can cancel them even during reentrant callbacks.
    std::uint64_t acknowledged_{};
    bool callbacks_enabled_{true};
    // Writer publication and admitted work are synchronized by mutex_.
    std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<std::shared_ptr<const library::RevisionDelta>> pending_;
    std::size_t pending_bytes_{};
    std::uint64_t latest_{}, durable_{}, attempted_{}, attempt_{};
    Result last_result_;
    bool active_{}, retry_{}, stopping_{}, has_durable_{};
    std::jthread worker_;
};

LibraryPersistence::LibraryPersistence(SnapshotWriter& store, Delivery delivery,
    library::LibrarySnapshot initial, Observer observer, LibraryPersistenceLimits limits)
    : state_{std::make_shared<State>(store, std::move(delivery), std::move(observer), limits)}
{
    // The worker retains its state until stop joins it. Scheduled owner callbacks retain
    // only weak state, and cannot keep a disposed workflow alive.
    state_->worker_ = std::jthread{[state = state_, initial = std::move(initial)]() mutable {
        state->run(std::move(initial));
    }};
}

LibraryPersistence::~LibraryPersistence()
{
    const auto state = state_;
    state->check_owner();
    state->callbacks_enabled_ = false;
    state->stop_worker();
    for (const auto& [revision, receipt] : state->receipts_)
        receipt->cancel();
    state->receipts_.clear();
    state->awaiters_.clear();
}

library::CommitResult
LibraryPersistence::accept(std::shared_ptr<const library::RevisionDelta> delta)
{
    const auto state = state_;
    state->check_owner();
    if (!delta || !state->callbacks_enabled_)
        return false;
    const bool critical = delta->priority == library::CommitPriority::file_result;
    const auto byte_limit = state->limits_.pending_payload_bytes +
        (critical ? state->limits_.file_result_extra_bytes : 0);
    const auto bytes = retained_bytes(*delta, byte_limit);
    if (!bytes)
        return false;
    const auto revision = delta->changes.revision;
    auto receipt = std::make_shared<library::CommitReceipt>(revision);
    {
        std::lock_guard lock{state->mutex_};
        if (state->stopping_ || revision != state->latest_ + 1 ||
            state->pending_.size() >= state->limits_.pending_revisions - (critical ? 0 : 1) ||
            state->latest_ - state->acknowledged_ >=
                state->limits_.outstanding_receipts - (critical ? 0 : 1) ||
            state->pending_bytes_ > byte_limit - *bytes)
            return false;
        state->pending_.push_back(std::move(delta));
        try {
            state->receipts_.emplace(revision, receipt);
        } catch (...) {
            state->pending_.pop_back();
            throw;
        }
        state->pending_bytes_ += *bytes;
        state->latest_ = revision;
    }
    state->changed_.notify_all();
    return library::CommitResult::accepted(revision, std::move(receipt));
}

std::uint64_t LibraryPersistence::pending_revision() const
{
    const auto state = state_;
    std::lock_guard lock{state->mutex_};
    return state->latest_;
}

std::uint64_t LibraryPersistence::durable_revision() const
{
    const auto state = state_;
    std::lock_guard lock{state->mutex_};
    return state->durable_;
}

void LibraryPersistence::retry()
{
    const auto state = state_;
    std::lock_guard lock{state->mutex_};
    if (!state->stopping_ && (!state->has_durable_ || state->durable_ < state->latest_)) {
        state->retry_ = true;
        state->changed_.notify_all();
    }
}

void LibraryPersistence::when_durable(std::uint64_t revision, std::function<void(bool)> observer)
{
    const auto state = state_;
    state->check_owner();
    if (!observer || !state->callbacks_enabled_)
        return;
    bool saved = false, unavailable = false;
    {
        std::lock_guard lock{state->mutex_};
        saved = state->has_durable_ && state->durable_ >= revision;
        unavailable = state->stopping_ || revision > state->latest_;
    }
    if (saved || unavailable) {
        observer(saved);
        return;
    }
    state->awaiters_.push_back({revision, std::move(observer)});
    retry();
}

void LibraryPersistence::State::run(library::LibrarySnapshot initial)
{
    std::map<library::MediaId, library::TrackRecord> tracks;
    std::map<library::PlaylistId, library::PlaylistRecord> playlists;
    std::map<library::WorkspaceId, library::WorkspaceRecord> workspaces;
    upsert(tracks, initial.tracks);
    upsert(playlists, initial.playlists);
    upsert(workspaces, initial.workspaces);
    initial = {};
    const auto weak = weak_from_this();
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
            // Publish acknowledgment before waking flush: draining includes this attempt.
            static_cast<void>(delivery_.post([weak, revision, result] {
                if (const auto state = weak.lock())
                    state->completed(revision, result);
            }));
        }
        changed_.notify_all();
    }
}

void LibraryPersistence::State::completed(std::uint64_t revision, Result result)
{
    check_owner();
    if (!callbacks_enabled_)
        return;
    std::vector<std::shared_ptr<library::CommitReceipt>> receipts;
    const auto last = receipts_.upper_bound(revision);
    receipts.reserve(static_cast<std::size_t>(std::distance(receipts_.begin(), last)));
    for (auto it = receipts_.begin(); it != last; ++it)
        receipts.push_back(it->second);
    std::vector<Awaiter> ready;
    ready.reserve(awaiters_.size());
    auto retained = awaiters_.begin();
    for (auto it = awaiters_.begin(); it != awaiters_.end(); ++it) {
        if (it->revision <= revision) {
            ready.push_back(std::move(*it));
        } else {
            if (retained != it)
                *retained = std::move(*it);
            ++retained;
        }
    }
    awaiters_.erase(retained, awaiters_.end());
    if (result.succeeded())
        acknowledged_ = std::max(acknowledged_, revision);

    std::exception_ptr failure;
    const auto invoke = [&](auto&& callback) {
        try {
            callback();
        } catch (...) {
            if (!failure)
                failure = std::current_exception();
        }
    };
    for (const auto& receipt : receipts) {
        invoke([&] { receipt->notify(result.succeeded()); });
        if (!callbacks_enabled_)
            break;
    }
    if (callbacks_enabled_ && result.succeeded())
        receipts_.erase(receipts_.begin(), receipts_.upper_bound(revision));
    if (callbacks_enabled_) {
        for (const auto& awaiter : ready) {
            invoke([&] { awaiter.observer(result.succeeded()); });
            if (!callbacks_enabled_)
                break;
        }
    }
    if (callbacks_enabled_ && observer_)
        invoke([&] { observer_(revision, result); });
    if (failure)
        std::rethrow_exception(failure);
}

LibraryPersistence::Result LibraryPersistence::flush()
{
    const auto state = state_;
    state->check_owner();
    for (;;) {
        Result result;
        std::uint64_t target;
        {
            std::unique_lock lock{state->mutex_};
            target = state->latest_;
            if (state->stopping_)
                return state->last_result_;
            const auto before = state->attempt_;
            if (!(state->has_durable_ && state->durable_ >= target)) {
                if (!state->active_ && state->pending_.empty())
                    state->retry_ = true;
                state->changed_.notify_all();
                state->changed_.wait(lock, [&] {
                    return (state->has_durable_ && state->durable_ >= target) ||
                        (state->attempt_ > before && state->attempted_ >= target);
                });
            }
            result = state->last_result_;
        }
        state->delivery_.drain();
        std::lock_guard lock{state->mutex_};
        if (!state->callbacks_enabled_)
            return state->last_result_; // Disposal joined the final admitted write.
        if (!result.succeeded() || state->latest_ == target)
            return result;
    }
}

void LibraryPersistence::stop()
{
    const auto state = state_;
    state->check_owner();
    state->stop_worker();
    state->delivery_.drain();
}

} // namespace wiola::app
