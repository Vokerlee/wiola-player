/**
 * @file
 * @brief Per-instance playback buffering policy.
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

#include <utils/units.hpp>

#include <chrono>
#include <cmath>

namespace wiola::engine {

/// How much audio a pipeline holds ahead of output. The default remains 250 ms.
struct BufferingConfig {
    units::Time duration{std::chrono::milliseconds{250}};

    [[nodiscard]] bool valid() const noexcept
    {
        return std::isfinite(duration.seconds()) && duration > units::Time{};
    }
};

} // namespace wiola::engine
