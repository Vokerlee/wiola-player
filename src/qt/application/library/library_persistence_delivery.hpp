/**
 * @file
 * @brief Qt queued delivery adapter for portable library persistence.
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

#include <qt/queued_delivery.hpp>
#include <wiola/app/library/library_persistence.hpp>

#include <utility>

namespace wiola::app {

/// The receiver outlives the persistence workflow; producer endpoints retain only weak state.
[[nodiscard]] inline LibraryPersistence::Delivery
library_persistence_delivery(QueuedDelivery& receiver)
{
    return {
        [endpoint = receiver.endpoint()](std::function<void()> task) {
            return endpoint.post(std::move(task));
        },
        [&receiver] { receiver.drain(); },
    };
}

} // namespace wiola::app
