/**
 * @file
 * @brief Application-owned playback preferences and one-time restoration.
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
#include "equalizer_control.hpp"
#include "queue_settings.hpp"
#include "volume_control.hpp"

namespace wiola::app {

class PlaybackPreferences final {
public:
    explicit PlaybackPreferences(PlaybackService& playback);

    [[nodiscard]] QueueSettings& queue() noexcept { return queue_; }

    [[nodiscard]] VolumeControl& volume() noexcept { return volume_; }

    [[nodiscard]] EqualizerControl& equalizer() noexcept { return equalizer_; }

    [[nodiscard]] std::size_t missing_tracks() const noexcept { return missing_; }

    [[nodiscard]] bool flush();

private:
    QSettings settings_;
    QueueSettings queue_;
    VolumeControl volume_;
    EqualizerControl equalizer_;
    std::size_t missing_{0};
};

} // namespace wiola::app
