/**
 * @file
 * @brief Native measurements of configured audio processing.
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

#include <wiola/audio/equalizer.hpp>
#include <wiola/audio/processing_chain.hpp>
#include <wiola/audio/volume.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <vector>

int main()
{
    const wiola::pcm::StreamSpec spec{wiola::units::Frequency{48000}, 2};
    constexpr std::size_t iterations = 5000;

    for (const std::size_t frames : {256, 1024, 2048}) {
        auto equalizer = wiola::core::Lease<wiola::audio::Equalizer>::make(spec);
        equalizer->set_enabled(true);
        equalizer->set_band_gain(0, 6.0F);
        auto volume = wiola::core::Lease<wiola::audio::Volume>::make();
        volume->set_gain(0.5F);
        const auto chain = wiola::core::Lease<wiola::audio::ProcessingChain>::make(
            std::initializer_list<wiola::core::Lease<wiola::audio::Stage>>{equalizer, volume}
        );
        wiola::audio::Stage::configure(chain, spec);
        std::vector<float> samples(spec.samples_per(wiola::pcm::Frames{frames}));
        double checksum = 0;
        const auto before = std::chrono::steady_clock::now();

        for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
            std::ranges::fill(samples, 0.25F);
            chain->process(samples);
            checksum += samples.front();
        }

        const auto elapsed =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - before).count();
        std::cout << "{\"benchmark\":\"audio_processing\",\"frames\":" << frames
                  << ",\"iterations\":" << iterations << ",\"elapsed_ms\":" << elapsed * 1000
                  << ",\"frames_per_second\":" << frames * iterations / elapsed
                  << ",\"checksum\":" << checksum << "}\n";
    }
}
