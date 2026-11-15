/**
 * @file
 * @brief Thumbnail, decoded-byte, and item limits owned by the artwork cache.
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

#include <cstddef>

namespace wiola::gui {

struct ArtworkCachePolicy {
    int cover_size{20};
    std::size_t covers_kept{512};
    std::size_t cached_bytes{16 * 1024 * 1024};

    [[nodiscard]] bool valid() const noexcept
    {
        return cover_size > 0 && covers_kept > 0 && cached_bytes > 0;
    }
};

} // namespace wiola::gui
