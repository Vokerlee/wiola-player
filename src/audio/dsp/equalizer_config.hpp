/**
 * @file
 * @brief Immutable equalizer layout and validated processing policy.
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

#include <utils/units.hpp>

#include <cmath>
#include <cstddef>

namespace wiola::audio {

/**
 * Where the bands sit: `count` centers from `first`, each `ratio` times the one before it.
 * A ratio of two is one band per octave, which is the default: ten of them from 31.25 Hz.
 */
struct BandLayout {
    static constexpr std::size_t max_count{64};

    units::Frequency first{31.25};
    std::size_t count{10};
    double ratio{2.0};
    double quality{1.0};

    [[nodiscard]] bool valid() const noexcept
    {
        return std::isfinite(first.hz()) && first > units::Frequency{} && count > 0 &&
            count <= max_count && std::isfinite(ratio) && ratio > 1.0 && std::isfinite(quality) &&
            quality > 0.0;
    }

    /// Where band `index` sits. Indices past `count` continue the series.
    [[nodiscard]] constexpr units::Frequency center(std::size_t index) const noexcept
    {
        units::Frequency value{first};

        for (std::size_t i = 0; i < index; ++i)
            value = value * ratio;

        return value;
    }
};

/// Immutable equalizer policy, separate from live gains and enabled state.
struct EqualizerConfig {
    BandLayout layout{};

    /// Highest center a band can receive, as a share of sample rate, below Nyquist.
    double highest_band_share{0.4};

    /// The most a band may lift or cut, in decibels.
    float max_band_gain_db{12.0F};

    [[nodiscard]] bool valid() const noexcept
    {
        return layout.valid() && std::isfinite(highest_band_share) && highest_band_share > 0.0 &&
            highest_band_share < 0.5 && std::isfinite(max_band_gain_db) && max_band_gain_db > 0.0F;
    }
};

} // namespace wiola::audio
