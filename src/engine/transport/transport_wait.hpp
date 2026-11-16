/**
 * @file
 * @brief Interruptible bounded waiting for the transport worker.
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

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>

namespace wiola::engine {

/**
 * A bounded output-progress poll that transport commands can interrupt.
 *
 * Only the worker waits. Any producer may notify. A generation observed by the worker prevents a
 * notification between iterations from being lost before the next wait. No audio-callback path
 * uses this class.
 */
class TransportWait {
public:
    explicit TransportWait(std::chrono::milliseconds poll_interval) noexcept;

    /// Publishes a new command generation and wakes the worker.
    void notify() noexcept;

    /// Waits for an unobserved notification or the configured output-progress interval.
    void wait();

private:
    std::chrono::milliseconds poll_interval_;
    std::mutex mutex_;
    std::condition_variable changed_;
    std::size_t published_{0};
    std::size_t observed_{0};
};

} // namespace wiola::engine
