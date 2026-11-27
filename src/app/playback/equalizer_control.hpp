/**
 * @file
 * @brief The equalizer as the window drives it.
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

#include "playback_service.hpp"
#include "playback_settings.hpp"

#include <wiola/units.hpp>

#include <cstddef>
#include <span>

namespace wiola::app {

/// Settings applied to `equalizer` and kept for the runs after this one. Nothing else may set
/// them, or what they are set to is not what comes back.
class EqualizerControl {
public:
    EqualizerControl(app::PlaybackService& equalizer, PlaybackSettingsWriter& settings) noexcept;

    /// Applies startup values for every layout band without writing them back. Missing gains are
    /// flat, and the domain applies its own gain bounds.
    void apply_restored(bool enabled, std::span<const float> gains) noexcept;

    void set_enabled(bool enabled);
    void set_band_gain(std::size_t index, float db);

    [[nodiscard]] bool enabled() const noexcept;
    /// The cut the equalizer took to make room for what its bands add.
    [[nodiscard]] float preamp() const noexcept;
    [[nodiscard]] float max_band_gain() const noexcept;
    [[nodiscard]] float band_gain(std::size_t index) const noexcept;
    [[nodiscard]] std::size_t num_bands() const noexcept;
    [[nodiscard]] units::Frequency band_center(std::size_t index) const noexcept;

private:
    app::PlaybackService& equalizer_;
    PlaybackSettingsWriter& settings_;
};

} // namespace wiola::app
