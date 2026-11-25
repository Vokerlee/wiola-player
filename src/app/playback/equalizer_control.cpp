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

#include "equalizer_control.hpp"

namespace wiola::app {

EqualizerControl::EqualizerControl(app::PlaybackService& equalizer,
    PlaybackSettingsWriter& settings) noexcept
    : equalizer_{equalizer}
    , settings_{settings}
{
}

void EqualizerControl::apply_restored(bool enabled, std::span<const float> gains) noexcept
{
    equalizer_.set_equalizer_enabled(enabled);

    // Every band the layout names, not only those this format can carry: a band left out now is
    // still set for the track that can.
    for (std::size_t index = 0; index < equalizer_.equalizer_config().layout.count; ++index)
        equalizer_.set_equalizer_band(index, index < gains.size() ? gains[index] : 0.0F);
}

void EqualizerControl::set_enabled(bool enabled)
{
    equalizer_.set_equalizer_enabled(enabled);
    settings_.write_equalizer_enabled(enabled);
}

void EqualizerControl::set_band_gain(std::size_t index, float db)
{
    if (index >= equalizer_.equalizer_config().layout.count)
        return;
    equalizer_.set_equalizer_band(index, db);
    settings_.write_band_gain(index, equalizer_.equalizer_band(index));
}

bool EqualizerControl::enabled() const noexcept
{
    return equalizer_.equalizer_enabled();
}

float EqualizerControl::preamp() const noexcept
{
    return equalizer_.equalizer_preamp();
}

float EqualizerControl::max_band_gain() const noexcept
{
    return equalizer_.equalizer_config().max_band_gain_db;
}

float EqualizerControl::band_gain(std::size_t index) const noexcept
{
    return equalizer_.equalizer_band(index);
}

std::size_t EqualizerControl::num_bands() const noexcept
{
    return equalizer_.num_equalizer_bands();
}

units::Frequency EqualizerControl::band_center(std::size_t index) const noexcept
{
    return equalizer_.equalizer_band_center(index);
}

} // namespace wiola::app
