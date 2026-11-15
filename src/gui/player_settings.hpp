/**
 * @file
 * @brief Typed persisted player settings and the Qt adapter that reads them.
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

#include <audio/dsp/equalizer_config.hpp>
#include <gui/volume_scale.hpp>

#include <QSettings>

#include <vector>

namespace wiola::gui {

/// Persisted values applied once at startup. No engine type knows their schema keys.
struct PlayerSettings {
    int volume_position{100};
    bool boosted{false};
    bool equalizer_enabled{true};
    std::vector<float> band_gains;
};

/// Converts Qt's string-keyed storage into typed values, with defaults for invalid old data.
class QtSettingsAdapter {
public:
    explicit QtSettingsAdapter(QSettings& settings) noexcept;

    /// Reads slider positions, switches, and per-band decibel gains for the supplied policies.
    [[nodiscard]] PlayerSettings read(const VolumeScale& volume,
        const audio::EqualizerConfig& equalizer) const;

private:
    QSettings& settings_;
};

} // namespace wiola::gui
