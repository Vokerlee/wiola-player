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
#include <memory>
#include <mutex>

namespace wiola::app {

/// Owner-thread receiver with weak producer endpoints. Endpoints remain safe after destruction.
class QueuedDelivery final : public QObject {
    struct State;

public:
    class Endpoint {
    public:
        [[nodiscard]] bool post(std::function<void()> delivery) const;

    private:
        friend class QueuedDelivery;

        explicit Endpoint(const std::shared_ptr<State>& state)
            : state_{state}
        {
        }

        std::weak_ptr<State> state_;
    };

    explicit QueuedDelivery(QObject& context);
    ~QueuedDelivery() override;

    [[nodiscard]] Endpoint endpoint() const { return Endpoint{state_}; }

    [[nodiscard]] bool post(std::function<void()> delivery)
    {
        return endpoint().post(std::move(delivery));
    }

    /// Stop accepting new payloads; queued payloads may still drain on the owner thread.
    void stop_accepting();
    void discard();
    void drain();

protected:
    void customEvent(QEvent* event) override;

private:
    struct State {
        std::mutex mutex;
        std::deque<std::function<void()>> pending;
        bool accepting{true};
        QueuedDelivery* receiver;
    };

    std::shared_ptr<State> state_;
};

} // namespace wiola::app
