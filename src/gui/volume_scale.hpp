/**
 * @file
 * @brief Presentation-only mapping between volume slider positions and gain.
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

namespace wiola::gui {

/// Slider scale, separate from the domain's maximum boost policy.
struct VolumeScale {
    int full_position{100};
    double curve{2.0};

    [[nodiscard]] float gain_of(int position) const noexcept
    {
        const double scaled{static_cast<double>(position) / full_position};
        return static_cast<float>(scaled > 1.0 ? scaled : std::pow(scaled, curve));
    }
};

} // namespace wiola::gui
