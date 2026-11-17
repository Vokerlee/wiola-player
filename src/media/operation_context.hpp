/**
 * @file
 * @brief Cooperative stop and wall-clock deadline state for media operations.
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

#include <media/error.hpp>

#include <chrono>
#include <optional>
#include <stop_token>

namespace wiola::media {

enum class StopReason {
    none,
    cancelled,
    timed_out,
};

struct OperationContext {
    using Clock = std::chrono::steady_clock;

    std::stop_token stop;
    std::optional<Clock::time_point> deadline;

    [[nodiscard]] StopReason stop_reason() const noexcept
    {
        if (stop.stop_requested())
            return StopReason::cancelled;

        if (deadline && Clock::now() >= *deadline)
            return StopReason::timed_out;

        return StopReason::none;
    }

    [[nodiscard]] Error error() const
    {
        const StopReason reason{stop_reason()};
        return {.category = reason == StopReason::timed_out ? ErrorCategory::timed_out
                                                            : ErrorCategory::cancelled,
            .detail = reason == StopReason::timed_out ? "media operation timed out"
                                                      : "media operation cancelled"};
    }
};

} // namespace wiola::media
