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
    struct Attempt {
        std::uint64_t number{};
        std::uint64_t revision{};
        Result result;
    };

    struct Pending {
        std::shared_ptr<const library::RevisionDelta> delta;
        std::size_t bytes;
    };

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
    void deliver(Attempt attempt);
    void completed(Attempt attempt);
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
    std::uint64_t delivered_attempt_{};
    bool callbacks_enabled_{true};
    // Writer publication and admitted work are synchronized by mutex_.
    std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<Pending> pending_;
    std::size_t pending_bytes_{};
    std::uint64_t latest_{}, durable_{};
    Attempt last_attempt_;
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
        state->pending_.push_back({std::move(delta), *bytes});
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
    bool initialized = false;
    const auto weak = weak_from_this();
    for (;;) {
        std::uint64_t revision;
        std::size_t pending_count;
        {
            std::unique_lock lock{mutex_};
            changed_.wait(lock,
                [this] { return stopping_ || retry_ || latest_ > last_attempt_.revision; });
            if (stopping_ && latest_ <= last_attempt_.revision && !retry_)
                return;
            retry_ = false;
            active_ = true;
            revision = latest_;
            pending_count = pending_.size();
        }
        Result result;
        try {
            if (!initialized) {
                upsert(tracks, initial.tracks);
                upsert(playlists, initial.playlists);
                upsert(workspaces, initial.workspaces);
                initial = {};
                initialized = true;
            }
            // Only this worker removes entries. Later admissions append beyond this prefix.
            for (std::size_t remaining = pending_count; remaining != 0; --remaining) {
                std::shared_ptr<const library::RevisionDelta> delta;
                {
                    std::lock_guard lock{mutex_};
                    delta = pending_.front().delta;
                }
                upsert(tracks, delta->tracks);
                upsert(playlists, delta->playlists);
                upsert(workspaces, delta->workspaces);
                remove(tracks, delta->changes.removed_media);
                remove(playlists, delta->changes.removed_playlists);
                remove(workspaces, delta->changes.removed_workspaces);
                {
                    // Retain a failed immutable delta and its capacity until every record
                    // has been applied. A retry replays it before any newer revision.
                    std::lock_guard lock{mutex_};
                    pending_bytes_ -= pending_.front().bytes;
                    pending_.pop_front();
                }
            }
            result = store_.write({values(tracks), values(playlists), values(workspaces)});
        } catch (...) {
            // Preparation and driver failures retain enough state to retry accepted edits.
            result.error = SnapshotWriteError::io;
        }
        {
            std::lock_guard lock{mutex_};
            active_ = false;
            last_attempt_ = {last_attempt_.number + 1, revision, result};
            if (result.succeeded()) {
                durable_ = revision;
                has_durable_ = true;
            }
            try {
                // Explicit flush/stop also deliver this attempt if scheduling rejects or throws.
                static_cast<void>(delivery_.post([weak, attempt = last_attempt_] {
                    if (const auto state = weak.lock())
                        state->completed(attempt);
                }));
            } catch (...) {
                // The write outcome remains available even when owner delivery is unavailable.
            }
        }
        changed_.notify_all();
    }
}

void LibraryPersistence::State::deliver(Attempt attempt)
{
    std::exception_ptr failure;
    try {
        delivery_.drain();
    } catch (...) {
        failure = std::current_exception();
    }
    try {
        completed(attempt);
    } catch (...) {
        if (!failure)
            failure = std::current_exception();
    }
    if (failure)
        std::rethrow_exception(failure);
}

void LibraryPersistence::State::completed(Attempt attempt)
{
    check_owner();
    if (!callbacks_enabled_ || attempt.number <= delivered_attempt_)
        return;
    const auto [number, revision, result] = attempt;
    std::optional<std::uint64_t> durable_revision;
    {
        std::lock_guard lock{mutex_};
        if (has_durable_)
            durable_revision = std::min(revision, durable_);
    }
    const auto saved = [durable_revision](std::uint64_t accepted_revision) {
        return durable_revision && accepted_revision <= *durable_revision;
    };

    struct ReceiptNotification {
        std::shared_ptr<library::CommitReceipt> receipt;
        bool durable;
    };

    std::vector<ReceiptNotification> receipts;
    const auto last = receipts_.upper_bound(revision);
    receipts.reserve(static_cast<std::size_t>(std::distance(receipts_.begin(), last)));
    for (auto it = receipts_.begin(); it != last; ++it)
        receipts.push_back({it->second, saved(it->first)});
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
    if (durable_revision)
        acknowledged_ = std::max(acknowledged_, *durable_revision);
    // Preparation above may allocate. Mark delivery only after it succeeds, and before
    // invoking observers that may flush again or dispose the workflow.
    delivered_attempt_ = number;

    std::exception_ptr failure;
    const auto invoke = [&](auto&& callback) {
        try {
            callback();
        } catch (...) {
            if (!failure)
                failure = std::current_exception();
        }
    };
    for (const auto& notification : receipts) {
        invoke([&] { notification.receipt->notify(notification.durable); });
        if (!callbacks_enabled_)
            break;
    }
    if (callbacks_enabled_)
        receipts_.erase(receipts_.begin(), receipts_.upper_bound(acknowledged_));
    if (callbacks_enabled_) {
        for (const auto& awaiter : ready) {
            invoke([&] { awaiter.observer(saved(awaiter.revision)); });
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
        State::Attempt attempt;
        std::uint64_t target;
        {
            std::unique_lock lock{state->mutex_};
            target = state->latest_;
            if (state->stopping_)
                return state->last_attempt_.result;
            const auto before = state->last_attempt_.number;
            if (!(state->has_durable_ && state->durable_ >= target)) {
                if (!state->active_ && state->pending_.empty())
                    state->retry_ = true;
                state->changed_.notify_all();
                state->changed_.wait(lock, [&] {
                    return (state->has_durable_ && state->durable_ >= target) ||
                        (state->last_attempt_.number > before &&
                            state->last_attempt_.revision >= target);
                });
            }
            attempt = state->last_attempt_;
        }
        state->deliver(attempt);
        std::lock_guard lock{state->mutex_};
        if (!state->callbacks_enabled_)
            return state->last_attempt_.result; // Disposal joined the final admitted write.
        if (!attempt.result.succeeded() || state->latest_ == target)
            return attempt.result;
    }
}

void LibraryPersistence::stop()
{
    const auto state = state_;
    state->check_owner();
    state->stop_worker();
    State::Attempt attempt;
    {
        std::lock_guard lock{state->mutex_};
        attempt = state->last_attempt_;
    }
    state->deliver(attempt);
}

} // namespace wiola::app
