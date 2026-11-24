/**
 * @file
 * @brief Realtime equalizer processing and filter history.
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

#include "equalizer_processor.hpp"

#include "biquad.hpp"
#include "equalizer_parameters.hpp"

namespace wiola::audio {

EqualizerProcessor::EqualizerProcessor(EqualizerParameters& parameters) noexcept
    : parameters_{parameters}
{
}

void EqualizerProcessor::configure(pcm::StreamSpec spec)
{
    const auto& bank{parameters_.acquire_for_processing()};
    num_bands_ = bank.num_bands;

    for (std::size_t index = 0; index < num_bands_; ++index)
        filters_[index].configure(bank.coefficients[index], spec.num_channels);
}

void EqualizerProcessor::process(std::span<float> samples) noexcept
{
    const auto& bank{parameters_.acquire_for_processing()};

    if (!bank.shaping || !bank.enabled)
        return;

    for (float& sample : samples)
        sample *= bank.preamp;

    for (std::size_t index = 0; index < num_bands_; ++index) {
        filters_[index].retune(bank.coefficients[index]);
        filters_[index].process(samples);
    }
}

} // namespace wiola::audio
