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

#include "equalizer_config.hpp"

#include <wiola/audio/stage.hpp>
#include <wiola/media/stream_spec.hpp>
#include <wiola/units.hpp>

#include <cstddef>
#include <memory>
#include <span>

namespace wiola::audio {

/// Combines listener-owned equalizer settings with their processing-thread filter state.
class Equalizer final : public Stage {
public:
    explicit Equalizer(pcm::StreamSpec spec, BandLayout layout = {});
    Equalizer(pcm::StreamSpec spec, EqualizerConfig config);

    Equalizer(const Equalizer&) = delete;
    Equalizer& operator=(const Equalizer&) = delete;
    Equalizer(Equalizer&&) = delete;
    Equalizer& operator=(Equalizer&&) = delete;

    ~Equalizer() override;

    [[nodiscard]] const EqualizerConfig& config() const noexcept;

    [[nodiscard]] const BandLayout& layout() const noexcept;

    [[nodiscard]] std::size_t num_bands() const noexcept;

    [[nodiscard]] units::Frequency band_center(std::size_t index) const noexcept;

    void set_band_gain(std::size_t index, float db) noexcept;
    [[nodiscard]] float band_gain(std::size_t index) const noexcept;

    [[nodiscard]] float preamp() const noexcept;

    void set_enabled(bool enabled) noexcept;

    [[nodiscard]] bool enabled() const noexcept;

    void process(std::span<float> samples) noexcept override;

private:
    /// Prepares independent processing storage; commit adopts the latest accepted gains
    /// and enabled state. Configuration references remain stable across commit/failure.
    [[nodiscard]] std::unique_ptr<Preparation> prepare_change(pcm::StreamSpec spec) override;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace wiola::audio
