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

#include "queued_delivery.hpp"
#include <QCoreApplication>
#include <QEvent>
#include <QThread>
#include <deque>
#include <mutex>

namespace wiola::app {
namespace {

QEvent::Type delivery_event_type()
{
    static const auto type = static_cast<QEvent::Type>(QEvent::registerEventType());
    return type;
}

} // namespace

struct QueuedDelivery::State {
    using Pending = std::deque<std::function<void()>>;

    std::mutex mutex;
    // Detach the whole queue on discard without allocating during receiver destruction.
    std::unique_ptr<Pending> pending{std::make_unique<Pending>()};
    bool accepting{true};
    QueuedDelivery* receiver{nullptr};
};

QueuedDelivery::QueuedDelivery(QObject& context)
    : state_{std::make_shared<State>()}
{
    Q_ASSERT(QThread::currentThread() == context.thread());
    state_->receiver = this;
    static_cast<void>(delivery_event_type());
    connect(&context, &QObject::destroyed, this, [this] { discard(); });
}

QueuedDelivery::~QueuedDelivery()
{
    discard();
}

bool QueuedDelivery::Endpoint::post(std::function<void()> delivery) const
{
    if (!delivery)
        return false;
    const auto state = state_.lock();
    if (!state)
        return false;
    std::lock_guard lock{state->mutex};
    if (!state->accepting || !state->receiver)
        return false;
    state->pending->push_back(std::move(delivery));
    // The receiver destructor takes this mutex before retiring the QObject address.
    QCoreApplication::postEvent(state->receiver, new QEvent{delivery_event_type()});
    return true;
}

void QueuedDelivery::stop_accepting()
{
    std::lock_guard lock{state_->mutex};
    state_->accepting = false;
}

void QueuedDelivery::discard()
{
    const auto state = state_;
    std::unique_ptr<State::Pending> discarded;
    {
        std::lock_guard lock{state->mutex};
        state->accepting = false;
        state->receiver = nullptr;
        discarded = std::move(state->pending);
    }
    // Captured objects may post back, discard again or destroy the receiver as they retire.
}

void QueuedDelivery::drain()
{
    Q_ASSERT(QThread::currentThread() == thread());
    const auto state = state_;
    for (;;) {
        std::function<void()> callback;
        {
            std::lock_guard lock{state->mutex};
            if (!state->pending || state->pending->empty())
                return;
            callback = std::move(state->pending->front());
            state->pending->pop_front();
        }
        callback();
    }
}

void QueuedDelivery::customEvent(QEvent* event)
{
    const auto state = state_;
    std::function<void()> callback;
    bool foreign = false;
    {
        // Acquire the producer publication before inspecting even the wakeup event. System Qt
        // may be uninstrumented; all first-party cross-thread payload access uses this boundary.
        std::lock_guard lock{state->mutex};
        foreign = event->type() != delivery_event_type();
        if (!foreign && state->pending && !state->pending->empty()) {
            callback = std::move(state->pending->front());
            state->pending->pop_front();
        }
    }
    if (foreign) {
        QObject::customEvent(event);
        return;
    }
    if (callback)
        callback();
}

} // namespace wiola::app
