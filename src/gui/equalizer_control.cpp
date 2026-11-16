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

#include <QString>

namespace wiola::gui {

namespace {

constexpr auto enabled_key{"equalizer/enabled"};

QString band_key(std::size_t index)
{
    return QString{"equalizer/band/%1"}.arg(index);
}

} // namespace

EqualizerControl::EqualizerControl(audio::Equalizer& equalizer, QSettings& settings) noexcept
    : equalizer_{equalizer}
    , settings_{settings}
{
}

void EqualizerControl::apply_restored(bool enabled, std::span<const float> gains) noexcept
{
    equalizer_.set_enabled(enabled);

    // Every band the layout names, not only those this format can carry: a band left out now is
    // still set for the track that can.
    for (std::size_t index = 0; index < equalizer_.layout().count; ++index)
        equalizer_.set_band_gain(index, index < gains.size() ? gains[index] : 0.0F);
}

void EqualizerControl::set_enabled(bool enabled)
{
    equalizer_.set_enabled(enabled);
    settings_.setValue(enabled_key, enabled);
}

void EqualizerControl::set_band_gain(std::size_t index, float db)
{
    equalizer_.set_band_gain(index, db);
    settings_.setValue(band_key(index), equalizer_.band_gain(index));
}

bool EqualizerControl::enabled() const noexcept
{
    return equalizer_.enabled();
}

float EqualizerControl::preamp() const noexcept
{
    return equalizer_.preamp();
}

float EqualizerControl::max_band_gain() const noexcept
{
    return equalizer_.config().max_band_gain_db;
}

float EqualizerControl::band_gain(std::size_t index) const noexcept
{
    return equalizer_.band_gain(index);
}

std::size_t EqualizerControl::num_bands() const noexcept
{
    return equalizer_.num_bands();
}

units::Frequency EqualizerControl::band_center(std::size_t index) const noexcept
{
    return equalizer_.band_center(index);
}

} // namespace wiola::gui
