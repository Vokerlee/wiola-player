/**
 * @file
 * @brief Qt owner delivery adapter for portable track projection.
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
#include <QThread>
#include <qt/queued_delivery.hpp>
#include <stdexcept>
#include <utility>

namespace wiola::gui {

std::unique_ptr<TrackProjection> make_track_projection(QObject& owner,
    TrackProjection::Observer observer, TrackProjection::Before before)
{
    if (QThread::currentThread() != owner.thread())
        throw std::logic_error{"projection receiver belongs to another thread"};
    auto receiver = std::make_shared<app::QueuedDelivery>(owner);
    const auto endpoint = receiver->endpoint();
    return std::make_unique<TrackProjection>(
        TrackProjection::Delivery{[endpoint](auto task) { return endpoint.post(std::move(task)); },
            [receiver] { receiver->discard(); }},
        std::move(observer), std::move(before)
    );
}

} // namespace wiola::gui
