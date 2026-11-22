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

#include "volume_control.hpp"

#include <algorithm>
#include <cmath>

namespace wiola::gui {

namespace {

/// Under which name the position is kept.
constexpr auto position_key{"volume"};
constexpr auto boosted_key{"volume/boosted"};

} // namespace

VolumeControl::VolumeControl(app::PlaybackService& volume, QSettings& settings) noexcept
    : volume_{volume}
    , settings_{settings}
    , position_{scale_.full_position}
{
}

void VolumeControl::apply_restored(int position, bool boosted) noexcept
{
    boosted_ = boosted;
    apply_position(position);
}

void VolumeControl::set_position(int percent)
{
    apply_position(percent);

    settings_.setValue(position_key, position_);
}

void VolumeControl::apply_position(int percent)
{
    position_ = std::clamp(percent, 0, max_position());

    volume_.set_volume(scale_.gain_of(position_));
}

void VolumeControl::set_boosted(bool boosted)
{
    boosted_ = boosted;
    settings_.setValue(boosted_key, boosted_);

    // What was asked for past full is not what a slider that stops at full may ask.
    set_position(position_);
}

int VolumeControl::position() const noexcept
{
    return position_;
}

bool VolumeControl::boosted() const noexcept
{
    return boosted_;
}

int VolumeControl::max_position() const noexcept
{
    return boosted_
        ? static_cast<int>(std::lround(volume_.volume_policy().max_boost * scale_.full_position))
        : scale_.full_position;
}

} // namespace wiola::gui
