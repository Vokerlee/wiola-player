/**
 * @file
 * @brief Stable media error categories and backend diagnostic detail.
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

#pragma once

#include <string>

namespace wiola::media {

enum class ErrorCategory {
    unreadable,
    not_mine,
    unsupported,
    damaged,
    read_failed,
    seek_failed,
    invalid_seek,
};

struct Error {
    ErrorCategory category;
    std::string detail;
};

} // namespace wiola::media
