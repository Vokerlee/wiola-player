/**
 * @file
 * @brief Interruptible bounded waiting for the transport worker.
 * @author Roman Glaz
 * @copyright © 2026, <vokerlee@gmail.com>
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

#include "transport_wait.hpp"

namespace wiola::engine {

TransportWait::TransportWait(std::chrono::milliseconds poll_interval) noexcept
    : poll_interval_{poll_interval}
{
}

void TransportWait::notify() noexcept
{
    published_.fetch_add(1, std::memory_order_release);
    changed_.notify_one();
}

void TransportWait::wait()
{
    std::unique_lock lock{mutex_};
    const std::size_t observed{observed_};

    changed_.wait_for(lock, poll_interval_,
        [this, observed] { return published_.load(std::memory_order_acquire) != observed; });
    observed_ = published_.load(std::memory_order_acquire);
}

} // namespace wiola::engine
