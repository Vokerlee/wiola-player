/**
 * @file
 * @brief Live equalizer settings and prepared coefficient banks.
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

#include <audio/dsp/equalizer_parameters.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace wiola::audio {

namespace {

float amplitude(float db) noexcept
{
    return static_cast<float>(std::pow(10.0, static_cast<double>(db) / 20.0));
}

} // namespace

EqualizerParameters::EqualizerParameters(pcm::StreamSpec spec, EqualizerConfig config)
    : config_{config}
{
    if (!config_.valid())
        throw std::invalid_argument{"invalid equalizer configuration"};

    configure(spec);
}

void EqualizerParameters::configure(pcm::StreamSpec spec) noexcept
{
    spec_ = spec;
    num_bands_ = 0;

    const units::Frequency highest{spec.sample_rate * config_.highest_band_share};
    while (num_bands_ < config_.layout.count && config_.layout.center(num_bands_) <= highest)
        ++num_bands_;

    publish();
}

units::Frequency EqualizerParameters::band_center(std::size_t index) const noexcept
{
    return config_.layout.center(index);
}

void EqualizerParameters::set_band_gain(std::size_t index, float db) noexcept
{
    if (index >= config_.layout.count || !std::isfinite(db))
        return;

    const float limit{config_.max_band_gain_db};
    gains_[index] = std::clamp(db, -limit, limit);
    publish();
}

float EqualizerParameters::band_gain(std::size_t index) const noexcept
{
    return index < config_.layout.count ? gains_[index] : 0.0F;
}

void EqualizerParameters::set_enabled(bool enabled) noexcept
{
    enabled_ = enabled;
    publish();
}

void EqualizerParameters::publish() noexcept
{
    Bank prepared{};
    prepared.num_bands = num_bands_;
    prepared.enabled = enabled_;

    float lifted{0.0F};
    for (std::size_t index = 0; index < num_bands_; ++index) {
        const float gain{gains_[index]};
        prepared.coefficients[index] =
            BiquadCoefficients::peaking(spec_, band_center(index), config_.layout.quality, gain);
        lifted = std::max(lifted, gain);
        prepared.shaping = prepared.shaping || gain != 0.0F;
    }

    preamp_db_ = -lifted;
    prepared.preamp = amplitude(preamp_db_);

    std::size_t state{state_.load(std::memory_order_acquire)};
    std::size_t desired{};
    do {
        const std::size_t published{state >> 8};
        const std::size_t active{state & 0xffU};
        std::size_t slot{0};
        while (slot == published || slot == active)
            ++slot;

        banks_[slot] = prepared;
        desired = (slot << 8) | active;
    } while (!state_.compare_exchange_weak(state, desired, std::memory_order_acq_rel,
        std::memory_order_acquire));
}

const EqualizerParameters::Bank& EqualizerParameters::acquire_for_processing() noexcept
{
    std::size_t state{state_.load(std::memory_order_acquire)};
    std::size_t desired{};
    do {
        const std::size_t published{state >> 8};
        desired = (published << 8) | published;
    } while (!state_.compare_exchange_weak(state, desired, std::memory_order_acq_rel,
        std::memory_order_acquire));

    return banks_[desired >> 8];
}

} // namespace wiola::audio
