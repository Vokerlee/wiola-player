/**
 * @file
 * @brief Validated gain limits owned by a volume processor.
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

#include <cmath>

namespace wiola::audio {

/// The most output may be lifted past what arrived, as a factor. The default remains 140%.
struct VolumePolicy {
    float max_boost{1.4F};

    [[nodiscard]] bool valid() const noexcept
    {
        return std::isfinite(max_boost) && max_boost >= 1.0F;
    }
};

} // namespace wiola::audio
