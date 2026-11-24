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
    struct Processing {
        Processing(pcm::StreamSpec spec, const EqualizerConfig& config)
            : parameters{spec, config}
            , processor{parameters}
        {
            processor.configure(spec);
        }

        EqualizerParameters parameters;
        EqualizerProcessor processor;
    };

    struct Prepared final : Stage::Preparation {
        Prepared(Impl& owner, pcm::StreamSpec spec)
            : owner_{owner}
            , processing_{std::make_unique<Processing>(spec, owner.config)}
        {
        }

    private:
        void activate() noexcept override
        {
            processing_->parameters.adopt_settings(owner_.processing->parameters);
            owner_.processing.swap(processing_);
        }

        Impl& owner_;
        std::unique_ptr<Processing> processing_;
    };

    Impl(pcm::StreamSpec spec, EqualizerConfig authored_config)
        : config{authored_config}
        , processing{std::make_unique<Processing>(spec, config)}
    {
    }

    const EqualizerConfig config;
    std::unique_ptr<Processing> processing;
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
    return impl_->config;
}

const BandLayout& Equalizer::layout() const noexcept
{
    return impl_->config.layout;
}

std::size_t Equalizer::num_bands() const noexcept
{
    return impl_->processing->parameters.num_bands();
}

float Equalizer::preamp() const noexcept
{
    return impl_->processing->parameters.preamp();
}

bool Equalizer::enabled() const noexcept
{
    return impl_->processing->parameters.enabled();
}

std::unique_ptr<Stage::Preparation> Equalizer::prepare(pcm::StreamSpec spec)
{
    return std::make_unique<Impl::Prepared>(*impl_, spec);
}

units::Frequency Equalizer::band_center(std::size_t index) const noexcept
{
    return impl_->processing->parameters.band_center(index);
}

void Equalizer::set_band_gain(std::size_t index, float db) noexcept
{
    impl_->processing->parameters.set_band_gain(index, db);
}

float Equalizer::band_gain(std::size_t index) const noexcept
{
    return impl_->processing->parameters.band_gain(index);
}

void Equalizer::set_enabled(bool enabled) noexcept
{
    impl_->processing->parameters.set_enabled(enabled);
}

void Equalizer::process(std::span<float> samples) noexcept
{
    impl_->processing->processor.process(samples);
}

} // namespace wiola::audio
