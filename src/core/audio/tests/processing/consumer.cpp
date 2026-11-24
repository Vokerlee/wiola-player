/**
 * @file
 * @brief Independent installed consumer of portable audio processing.
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

#if __has_include(<processing/equalizer_parameters.hpp>) || __has_include(<core/macros.hpp>)
#error "The installed processing target must not expose private implementation or the source root"
#endif

#include <wiola/audio/equalizer.hpp>
#include <wiola/audio/processing_chain.hpp>
#include <wiola/audio/volume.hpp>

#include <array>
#include <cmath>

int main()
{
    wiola::audio::Equalizer equalizer{{}};
    wiola::audio::Volume volume;
    volume.set_gain(0.5F);
    wiola::audio::ProcessingChain chain{
        {equalizer, volume}
    };
    chain.configure({});
    std::array samples{0.5F, -0.5F};
    chain.process(samples);
    if (samples[0] != 0.25F || samples[1] != -0.25F)
        return 1;
    equalizer.set_band_gain(0, 3.0F);
    chain.process(samples);
    return !std::isfinite(samples[0]) || !equalizer.enabled();
}
