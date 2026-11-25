/**
 * @file
 * @brief Typed playback preferences and their persistence write port.
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

#include <cstddef>
#include <vector>

namespace wiola::app {

struct PlayerSettings {
    int volume_position{100};
    bool boosted{false};
    bool equalizer_enabled{true};
    std::vector<float> band_gains;
};

/// Semantic writes; the storage adapter owns key names, encoding and flush diagnostics.
class PlaybackSettingsWriter {
public:
    virtual ~PlaybackSettingsWriter() = default;
    virtual void write_volume_position(int position) = 0;
    virtual void write_boosted(bool boosted) = 0;
    virtual void write_equalizer_enabled(bool enabled) = 0;
    virtual void write_band_gain(std::size_t index, float db) = 0;
};

} // namespace wiola::app
