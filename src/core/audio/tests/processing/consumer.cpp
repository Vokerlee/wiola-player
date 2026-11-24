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
#include <memory>

namespace {

// A downstream stage implements only the public preparation/processing contract.
class StreamObserver final : public wiola::audio::Stage {
public:
    std::unique_ptr<Preparation> prepare(wiola::pcm::StreamSpec spec) override
    {
        return std::make_unique<Prepared>(*this, spec);
    }

    void process(std::span<float>) noexcept override { }

    wiola::pcm::StreamSpec configured{};
    unsigned commits{0};

private:
    class Prepared final : public Preparation {
    public:
        Prepared(StreamObserver& owner, wiola::pcm::StreamSpec spec) noexcept
            : owner_{owner}
            , spec_{spec}
        {
        }

    private:
        void activate() noexcept override
        {
            owner_.configured = spec_;
            ++owner_.commits;
        }

        StreamObserver& owner_;
        wiola::pcm::StreamSpec spec_;
    };
};

} // namespace

int main()
{
    wiola::audio::Equalizer equalizer{{}};
    wiola::audio::Volume volume;
    StreamObserver observer;
    volume.set_gain(0.5F);
    wiola::audio::ProcessingChain chain{
        {equalizer, volume, observer}
    };
    const auto prepared{chain.prepare({})};
    if (!prepared || observer.commits != 0)
        return 2;
    prepared->commit();
    prepared->commit();
    if (observer.commits != 1 || observer.configured != wiola::pcm::StreamSpec{})
        return 3;
    std::array samples{0.5F, -0.5F};
    chain.process(samples);
    if (samples[0] != 0.25F || samples[1] != -0.25F)
        return 1;
    equalizer.set_band_gain(0, 3.0F);
    chain.process(samples);
    return !std::isfinite(samples[0]) || !equalizer.enabled();
}
