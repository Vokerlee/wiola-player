/**
 * @file
 * @brief Typed persisted player settings and the Qt adapter that reads them.
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

#include <audio/dsp/equalizer_config.hpp>
#include <gui/volume_scale.hpp>

#include <QSettings>

#include <filesystem>
#include <optional>
#include <vector>

namespace wiola::gui {

/// Persisted values applied once at startup. No engine type knows their schema keys.
struct PlayerSettings {
    int volume_position{100};
    bool boosted{false};
    bool equalizer_enabled{true};
    std::vector<float> band_gains;
};

struct PlaylistSettings {
    std::vector<std::filesystem::path> tracks;
    std::optional<std::filesystem::path> selected;
    int repeat{0};
    bool shuffled{false};
};

/// Converts Qt's string-keyed storage into typed values, with defaults for invalid old data.
class QtSettingsAdapter {
public:
    explicit QtSettingsAdapter(QSettings& settings) noexcept;

    /// Reads slider positions, switches, and per-band decibel gains for the supplied policies.
    [[nodiscard]] PlayerSettings read(const VolumeScale& volume,
        const audio::EqualizerConfig& equalizer) const;
    [[nodiscard]] PlaylistSettings read_playlist() const;
    void write_playlist(const PlaylistSettings& settings);

private:
    QSettings& settings_;
};

} // namespace wiola::gui
