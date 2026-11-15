/**
 * @file
 * @brief Thumbnail and work limits owned by the artwork cache.
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

/// Thumbnail size, per-turn work limit, and item bound for one artwork cache.
struct ArtworkCachePolicy {
    int cover_size{20};
    std::size_t reads_per_turn{4};
    std::size_t covers_kept{512};

    [[nodiscard]] bool valid() const noexcept
    {
        return cover_size > 0 && reads_per_turn > 0 && covers_kept > 0;
    }
};

} // namespace wiola::gui
