/**
 * @file
 * @brief Application-level open status, including asynchronous loading.
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

namespace wiola::engine {

enum class OpenResult {
    opened,
    loading,
    unreadable,
    unsupported,
    damaged,
};

[[nodiscard]] constexpr OpenResult open_failed(media::ErrorCategory category) noexcept
{
    switch (category) {
    case media::ErrorCategory::unsupported:
        return OpenResult::unsupported;
    case media::ErrorCategory::damaged:
        return OpenResult::damaged;
    default:
        return OpenResult::unreadable;
    }
}

} // namespace wiola::engine
