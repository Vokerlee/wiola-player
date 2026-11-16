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

#pragma once

#include <audio/dsp/equalizer_parameters.hpp>
#include <audio/dsp/equalizer_processor.hpp>
#include <audio/dsp/stage.hpp>
#include <core/macros.hpp>
#include <pcm/stream_spec.hpp>
#include <utils/units.hpp>

#include <cstddef>
#include <span>

namespace wiola::audio {

/// Combines listener-owned equalizer settings with their processing-thread filter state.
class Equalizer final : public Stage {
public:
    explicit Equalizer(pcm::StreamSpec spec, BandLayout layout = {});
    Equalizer(pcm::StreamSpec spec, EqualizerConfig config);

    NO_COPY_SEMANTIC(Equalizer);
    NO_MOVE_SEMANTIC(Equalizer);

    /// The output must be stopped while configuring for another stream.
    void configure(pcm::StreamSpec spec) override;

    [[nodiscard]] const EqualizerConfig& config() const noexcept { return parameters_.config(); }

    [[nodiscard]] const BandLayout& layout() const noexcept { return parameters_.layout(); }

    [[nodiscard]] std::size_t num_bands() const noexcept { return parameters_.num_bands(); }

    [[nodiscard]] units::Frequency band_center(std::size_t index) const noexcept;

    void set_band_gain(std::size_t index, float db) noexcept;
    [[nodiscard]] float band_gain(std::size_t index) const noexcept;

    [[nodiscard]] float preamp() const noexcept { return parameters_.preamp(); }

    void set_enabled(bool enabled) noexcept;

    [[nodiscard]] bool enabled() const noexcept { return parameters_.enabled(); }

    void process(std::span<float> samples) noexcept override;

private:
    EqualizerParameters parameters_;
    EqualizerProcessor processor_;
};

} // namespace wiola::audio
