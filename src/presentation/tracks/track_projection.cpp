/**
 * @file
 * @brief Portable coalesced projection and callback lifetime management.
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

#include "track_projection.hpp"
#include "track_view_pipeline.hpp"

#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>

namespace wiola::gui {

struct TrackProjection::State : std::enable_shared_from_this<State> {
    struct Request {
        std::uint64_t generation;
        std::shared_ptr<const TrackProjectionInput> input;
        TrackViewState view;
        std::shared_ptr<std::stop_source> cancellation;
    };

    State(Delivery delivery, Observer observer, Before before)
        : delivery_{std::move(delivery)}
        , observer_{std::move(observer)}
        , before_{std::move(before)}
    {
        if (!delivery_.post || !delivery_.discard || !observer_)
            throw std::invalid_argument{"projection requires delivery and an observer"};
    }

    void check_owner() const
    {
        if (std::this_thread::get_id() != owner_)
            throw std::logic_error{"track projection accessed outside owner thread"};
    }

    void deliver()
    {
        check_owner();

        std::optional<TrackProjectionResult> result;
        {
            std::lock_guard lock{mutex_};

            posted_ = false;
            result = std::exchange(completed_, {});

            // Publication admits only the current generation; owner mutations clear this slot.
            if (!result)
                return;
        }
        observer_(std::move(*result));
    }

    void run()
    {
        const auto weak = weak_from_this();
        for (;;) {
            std::optional<Request> request;
            {
                std::unique_lock lock{mutex_};
                changed_.wait(lock, [this] { return stopping_ || pending_.has_value(); });

                if (stopping_)
                    return;

                request = std::exchange(pending_, {});
                active_ = request->cancellation;
            }

            const auto stop = request->cancellation->get_token();
            TrackProjectionResult result{.generation = request->generation,
                .rows = {},
                .failure = {}};

            try {
                if (before_)
                    before_(request->generation, stop);
                if (!stop.stop_requested())
                    result.rows =
                        apply_track_view(request->input->rows(), nullptr, request->view.query,
                            request->view.sort_keys, nullptr, &request->input->search(), stop);
            } catch (...) {
                result.failure = std::current_exception();
            }

            std::optional<TrackProjectionResult> previous;
            bool wake = false;
            {
                std::lock_guard lock{mutex_};

                active_.reset();

                if (stopping_ || stop.stop_requested() || request->generation != generation_)
                    continue;

                previous = std::exchange(completed_, std::move(result));

                if (!posted_) {
                    posted_ = true;
                    wake = true;
                }
            }

            if (wake) {
                bool accepted = false;

                try {
                    accepted = delivery_.post([weak] {
                        if (const auto state = weak.lock())
                            state->deliver();
                    });
                } catch (...) {
                    // A closed/failing receiver cannot terminate the computation worker.
                }

                if (!accepted) {
                    std::lock_guard lock{mutex_};
                    posted_ = false;
                }
            }
        }
    }

    void cancel_active(const std::shared_ptr<std::stop_source>& active)
    {
        if (!active)
            return;

        ++cancellation_depth_;
        active->request_stop();
        --cancellation_depth_;
    }

    void join_if_closed()
    {
        // A stop callback can close/dispose the facade while its worker waits for that
        // callback to return. The outer owner operation joins after cancellation unwinds.
        if (stopping_ && cancellation_depth_ == 0 && worker_.joinable())
            worker_.join();
    }

    void close()
    {
        check_owner();

        std::optional<Request> pending;
        std::optional<TrackProjectionResult> completed;
        std::shared_ptr<std::stop_source> active;
        {
            std::lock_guard lock{mutex_};

            if (stopping_)
                return;

            stopping_ = true;
            ++generation_;
            pending = std::exchange(pending_, {});
            completed = std::exchange(completed_, {});
            active = active_;
        }
        // stop callbacks and shared-payload destruction may reenter the owner.
        cancel_active(active);
        changed_.notify_all();

        std::exception_ptr failure;

        try {
            delivery_.discard();
        } catch (...) {
            failure = std::current_exception();
        }

        join_if_closed();

        if (failure)
            std::rethrow_exception(failure);
    }

    const std::thread::id owner_{std::this_thread::get_id()};
    const Delivery delivery_;
    Observer observer_;
    Before before_;
    std::size_t cancellation_depth_{};
    std::mutex mutex_;
    std::condition_variable changed_;
    std::optional<Request> pending_;
    std::optional<TrackProjectionResult> completed_;
    std::shared_ptr<std::stop_source> active_;
    std::uint64_t generation_{};
    bool stopping_{}, posted_{};
    std::jthread worker_;
};

TrackProjection::TrackProjection(Delivery delivery, Observer observer, Before before)
    : state_{std::make_shared<State>(std::move(delivery), std::move(observer), std::move(before))}
{
    state_->worker_ = std::jthread{[state = state_] { state->run(); }};
}

TrackProjection::~TrackProjection()
{
    state_->check_owner();
    try {
        state_->close();
    } catch (...) {
        // Explicit stop reports receiver failures; destruction still finishes the joined worker.
    }
}

std::uint64_t TrackProjection::request(std::shared_ptr<const TrackProjectionInput> input,
    TrackViewState view)
{
    const auto state = state_;
    state->check_owner();

    if (!input)
        return 0;

    State::Request request{0, std::move(input), normalized_track_view_state(std::move(view)),
        std::make_shared<std::stop_source>()};
    std::optional<State::Request> previous;
    std::optional<TrackProjectionResult> completed;
    std::shared_ptr<std::stop_source> active;
    std::uint64_t generation;
    {
        std::lock_guard lock{state->mutex_};

        if (state->stopping_)
            return 0;

        generation = request.generation = ++state->generation_;
        previous = std::exchange(state->pending_, std::move(request));
        completed = std::exchange(state->completed_, {});
        active = state->active_;
    }
    state->cancel_active(active);
    state->changed_.notify_all();
    state->join_if_closed();

    return generation;
}

void TrackProjection::cancel()
{
    const auto state = state_;
    state->check_owner();

    std::optional<State::Request> pending;
    std::optional<TrackProjectionResult> completed;
    std::shared_ptr<std::stop_source> active;
    {
        std::lock_guard lock{state->mutex_};

        if (state->stopping_)
            return;

        ++state->generation_;
        pending = std::exchange(state->pending_, {});
        completed = std::exchange(state->completed_, {});
        active = state->active_;
    }
    state->cancel_active(active);
    state->join_if_closed();
}

void TrackProjection::stop()
{
    const auto state = state_;
    state->close();
}

} // namespace wiola::gui
