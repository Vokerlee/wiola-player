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

#include <processing/equalizer_parameters.hpp>
#include <processing/equalizer_processor.hpp>
#include <wiola/audio/equalizer.hpp>

#include <memory>

namespace wiola::audio {

namespace {

EqualizerConfig config_for(BandLayout layout) noexcept
{
    EqualizerConfig config;
    config.layout = layout;
    return config;
}

} // namespace

struct Equalizer::Impl {
    Impl(pcm::StreamSpec spec, EqualizerConfig config)
        : parameters{spec, config}
        , processor{parameters}
    {
        processor.configure(spec);
    }

    EqualizerParameters parameters;
    EqualizerProcessor processor;
};

Equalizer::Equalizer(pcm::StreamSpec spec, BandLayout layout)
    : Equalizer{spec, config_for(layout)}
{
}

Equalizer::Equalizer(pcm::StreamSpec spec, EqualizerConfig config)
    : impl_{std::make_unique<Impl>(spec, config)}
{
}

Equalizer::~Equalizer() = default;

const EqualizerConfig& Equalizer::config() const noexcept
{
    return impl_->parameters.config();
}

const BandLayout& Equalizer::layout() const noexcept
{
    return impl_->parameters.layout();
}

std::size_t Equalizer::num_bands() const noexcept
{
    return impl_->parameters.num_bands();
}

float Equalizer::preamp() const noexcept
{
    return impl_->parameters.preamp();
}

bool Equalizer::enabled() const noexcept
{
    return impl_->parameters.enabled();
}

void Equalizer::configure(pcm::StreamSpec spec)
{
    impl_->parameters.configure(spec);
    impl_->processor.configure(spec);
}

units::Frequency Equalizer::band_center(std::size_t index) const noexcept
{
    return impl_->parameters.band_center(index);
}

void Equalizer::set_band_gain(std::size_t index, float db) noexcept
{
    impl_->parameters.set_band_gain(index, db);
}

float Equalizer::band_gain(std::size_t index) const noexcept
{
    return impl_->parameters.band_gain(index);
}

void Equalizer::set_enabled(bool enabled) noexcept
{
    impl_->parameters.set_enabled(enabled);
}

void Equalizer::process(std::span<float> samples) noexcept
{
    impl_->processor.process(samples);
}

} // namespace wiola::audio
