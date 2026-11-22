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
#include <utility>

namespace wiola::app {
namespace {

QEvent::Type delivery_event_type()
{
    static const auto type = static_cast<QEvent::Type>(QEvent::registerEventType());
    return type;
}

} // namespace

QueuedDelivery::QueuedDelivery(QObject& context)
{
    Q_ASSERT(QThread::currentThread() == context.thread());
    static_cast<void>(delivery_event_type());
    connect(&context, &QObject::destroyed, this, [this] {
        std::lock_guard lock{mutex_};
        accepting_ = false;
        pending_.clear();
    });
}

bool QueuedDelivery::post(std::function<void()> delivery)
{
    if (!delivery)
        return false;
    std::lock_guard lock{mutex_};
    if (!accepting_)
        return false;
    pending_.push_back(std::move(delivery));
    QCoreApplication::postEvent(this, new QEvent{delivery_event_type()});
    return true;
}

void QueuedDelivery::drain()
{
    Q_ASSERT(QThread::currentThread() == thread());
    for (;;) {
        std::function<void()> delivery;
        {
            std::lock_guard lock{mutex_};
            if (pending_.empty())
                return;
            delivery = std::move(pending_.front());
            pending_.pop_front();
        }
        delivery();
    }
}

void QueuedDelivery::customEvent(QEvent* event)
{
    std::function<void()> delivery;
    {
        std::lock_guard lock{mutex_};
        if (event->type() != delivery_event_type()) {
            QObject::customEvent(event);
            return;
        }
        if (pending_.empty())
            return;
        delivery = std::move(pending_.front());
        pending_.pop_front();
    }
    delivery();
}

} // namespace wiola::app
