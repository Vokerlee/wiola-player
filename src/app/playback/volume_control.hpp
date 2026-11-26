/**
 * @file
 * @brief The volume as the window drives it.
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

#include <wiola/app/playback/volume_scale.hpp>

#include "playback_service.hpp"

#include "playback_settings.hpp"

namespace wiola::app {

/// A slider position out of a hundred, applied to `volume` and kept for the runs after this
/// one. Nothing else may set the volume, or what it is set to is not what comes back.
class VolumeControl {
public:
    VolumeControl(app::PlaybackService& volume, PlaybackSettingsWriter& settings) noexcept;

    /// Applies startup values without writing them back. The domain clamps the position as usual.
    void apply_restored(int position, bool boosted) noexcept;

    void set_position(int percent);

    /// Whether the slider may ask for more than arrived. Turning it off brings a position that
    /// was past full back to it.
    void set_boosted(bool boosted);

    [[nodiscard]] int position() const noexcept;
    [[nodiscard]] bool boosted() const noexcept;

    [[nodiscard]] const app::VolumeScale& scale() const noexcept { return scale_; }

    /// The furthest the slider goes, which is what `boosted` decides.
    [[nodiscard]] int max_position() const noexcept;

private:
    /// Applies `percent` without keeping it, for a position that came out of the file already.
    void apply_position(int percent);

    app::PlaybackService& volume_;
    PlaybackSettingsWriter& settings_;
    app::VolumeScale scale_;
    int position_;
    bool boosted_{false};
};

} // namespace wiola::app
