/**
 * @file
 * @brief One presentation pipeline for filtering and sorting track rows.
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

#include <QThread>
#include <gui/projection/track_projection.hpp>
#include <presentation/tracks/track_view_pipeline.hpp>

namespace wiola::gui {

TrackProjection::TrackProjection(QObject& owner, Observer observer, Before before)
    : delivery_{owner}
    , lifetime_{std::make_shared<Lifetime>(Lifetime{this})}
    , observer_{std::move(observer)}
    , before_{std::move(before)}
    , worker_{[this] { run(); }}
{
}

TrackProjection::~TrackProjection()
{
    lifetime_->owner = nullptr;
    stop();
}

std::uint64_t TrackProjection::request(std::shared_ptr<const TrackProjectionInput> input,
    TrackViewState state)
{
    Q_ASSERT(QThread::currentThread() == delivery_.thread());
    std::lock_guard lock{mutex_};
    if (stopping_ || !input)
        return 0;
    if (active_)
        active_->request_stop();
    completed_.reset();
    pending_ = Request{++generation_, std::move(input),
        normalized_track_view_state(std::move(state)), std::make_shared<std::stop_source>()};
    changed_.notify_all();
    return generation_;
}

void TrackProjection::cancel()
{
    Q_ASSERT(QThread::currentThread() == delivery_.thread());
    std::lock_guard lock{mutex_};
    ++generation_;
    if (active_)
        active_->request_stop();
    pending_.reset();
    completed_.reset();
}

void TrackProjection::run()
{
    const auto endpoint = delivery_.endpoint();
    const std::weak_ptr<Lifetime> weak = lifetime_;
    for (;;) {
        std::optional<Request> request;
        {
            std::unique_lock lock{mutex_};
            changed_.wait(lock, [this] { return stopping_ || pending_.has_value(); });
            if (stopping_)
                return;
            request = std::move(pending_);
            pending_.reset();
            active_ = request->cancellation;
        }
        const auto stop = request->cancellation->get_token();
        if (before_)
            before_(request->generation, stop);
        auto rows = stop.stop_requested()
            ? std::vector<TrackRow>{}
            : apply_track_view(request->input->rows, nullptr, request->state.query,
                  request->state.sort_keys, nullptr, &request->input->search, stop);
        {
            std::lock_guard lock{mutex_};
            active_.reset();
            if (stopping_ || stop.stop_requested() || request->generation != generation_)
                continue;
            completed_ = TrackProjectionResult{request->generation, std::move(rows)};
            if (!posted_) {
                posted_ = endpoint.post([weak] {
                    if (const auto state = weak.lock(); state && state->owner)
                        state->owner->deliver();
                });
            }
        }
    }
}

void TrackProjection::deliver()
{
    Q_ASSERT(QThread::currentThread() == delivery_.thread());
    std::optional<TrackProjectionResult> result;
    {
        std::lock_guard lock{mutex_};
        posted_ = false;
        result = std::move(completed_);
        completed_.reset();
        if (!result || result->generation != generation_ || stopping_)
            return;
    }
    const auto observer = observer_;
    if (observer)
        observer(std::move(*result));
}

void TrackProjection::stop()
{
    Q_ASSERT(QThread::currentThread() == delivery_.thread());
    {
        std::lock_guard lock{mutex_};
        stopping_ = true;
        ++generation_;
        if (active_)
            active_->request_stop();
        pending_.reset();
        completed_.reset();
    }
    changed_.notify_all();
    delivery_.discard();
    if (worker_.joinable())
        worker_.join();
}

} // namespace wiola::gui
