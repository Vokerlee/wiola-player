/**
 * @file
 * @brief Lift and cut, one band at a time.
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

#include <audio/dsp/equalizer.hpp>

namespace wiola::audio {

namespace {

EqualizerConfig config_for(BandLayout layout) noexcept
{
    EqualizerConfig config;
    config.layout = layout;
    return config;
}

} // namespace

Equalizer::Equalizer(pcm::StreamSpec spec, BandLayout layout)
    : Equalizer{spec, config_for(layout)}
{
}

Equalizer::Equalizer(pcm::StreamSpec spec, EqualizerConfig config)
    : parameters_{spec, config}
    , processor_{parameters_}
{
    processor_.configure(spec);
}

void Equalizer::configure(pcm::StreamSpec spec)
{
    parameters_.configure(spec);
    processor_.configure(spec);
}

units::Frequency Equalizer::band_center(std::size_t index) const noexcept
{
    return parameters_.band_center(index);
}

void Equalizer::set_band_gain(std::size_t index, float db) noexcept
{
    parameters_.set_band_gain(index, db);
}

float Equalizer::band_gain(std::size_t index) const noexcept
{
    return parameters_.band_gain(index);
}

void Equalizer::set_enabled(bool enabled) noexcept
{
    parameters_.set_enabled(enabled);
}

void Equalizer::process(std::span<float> samples) noexcept
{
    processor_.process(samples);
}

} // namespace wiola::audio
