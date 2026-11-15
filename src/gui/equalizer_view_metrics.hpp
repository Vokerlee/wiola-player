/**
 * @file
 * @brief Presentation metrics owned by the equalizer panel.
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

namespace wiola::gui {

/// Equalizer slider geometry and integer-to-decibel mapping.
struct EqualizerViewMetrics {
    int band_slider_height{110};
    int band_column_width{42};
    int gain_steps_per_db{10};
    int gain_step{5};
};

} // namespace wiola::gui
