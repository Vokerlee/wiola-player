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

#pragma once

#include <audio/dsp/biquad.hpp>
#include <audio/dsp/equalizer_config.hpp>
#include <core/macros.hpp>
#include <pcm/stream_spec.hpp>

#include <array>
#include <cstddef>
#include <span>

namespace wiola::audio {

class EqualizerParameters;

/// Processing-thread owner of filter history. Processing allocates, locks, and destroys nothing.
class EqualizerProcessor {
public:
    explicit EqualizerProcessor(EqualizerParameters& parameters) noexcept;

    NO_COPY_SEMANTIC(EqualizerProcessor);
    NO_MOVE_SEMANTIC(EqualizerProcessor);

    /// Preallocates history for `spec`. The output must be stopped while configuring.
    void configure(pcm::StreamSpec spec);

    /// Applies one complete prepared bank at this block boundary.
    void process(std::span<float> samples) noexcept;

private:
    EqualizerParameters& parameters_;
    std::array<Biquad, BandLayout::max_count> filters_{};
    std::size_t num_bands_{0};
};

} // namespace wiola::audio
