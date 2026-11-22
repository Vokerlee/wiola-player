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

#pragma once
#include <QObject>
#include <deque>
#include <functional>
#include <mutex>

namespace wiola::app {

/// Moves payloads through an instrumented mutex, using Qt events only as wakeups. Producers must
/// stop before this owner-thread receiver is destroyed. Context destruction discards deliveries.
class QueuedDelivery final : public QObject {
public:
    explicit QueuedDelivery(QObject& context);
    [[nodiscard]] bool post(std::function<void()> delivery);

protected:
    void customEvent(QEvent* event) override;

private:
    std::mutex mutex_;
    std::deque<std::function<void()>> pending_;
    bool accepting_{true};
};

} // namespace wiola::app
