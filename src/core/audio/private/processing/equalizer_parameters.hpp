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

#pragma once

#include <processing/equalizer_publication.hpp>
#include <wiola/audio/equalizer_config.hpp>
#include <wiola/media/stream_spec.hpp>
#include <wiola/units.hpp>

#include <array>
#include <cstddef>

namespace wiola::audio {

/**
 * Listener-owned equalizer settings. One control thread writes and reads this object; the
 * processing thread receives complete immutable banks through `EqualizerProcessor`.
 */
class EqualizerParameters {
public:
    EqualizerParameters(pcm::StreamSpec spec, EqualizerConfig config = {});

    EqualizerParameters(const EqualizerParameters&) = delete;
    EqualizerParameters& operator=(const EqualizerParameters&) = delete;
    EqualizerParameters(EqualizerParameters&&) = delete;
    EqualizerParameters& operator=(EqualizerParameters&&) = delete;

    /// Rebuilds prepared banks for `spec`. The output must be stopped while configuring.
    void configure(pcm::StreamSpec spec) noexcept;

    [[nodiscard]] const EqualizerConfig& config() const noexcept { return config_; }

    [[nodiscard]] const BandLayout& layout() const noexcept { return config_.layout; }

    [[nodiscard]] std::size_t num_bands() const noexcept { return num_bands_; }

    [[nodiscard]] units::Frequency band_center(std::size_t index) const noexcept;

    void set_band_gain(std::size_t index, float db) noexcept;
    [[nodiscard]] float band_gain(std::size_t index) const noexcept;

    [[nodiscard]] float preamp() const noexcept { return preamp_db_; }

    void set_enabled(bool enabled) noexcept;

    [[nodiscard]] bool enabled() const noexcept { return enabled_; }

private:
    void publish() noexcept;
    [[nodiscard]] const EqualizerPublication::Bank& acquire_for_processing() noexcept;

    EqualizerConfig config_;
    pcm::StreamSpec spec_{};
    std::size_t num_bands_{0};
    std::array<float, BandLayout::max_count> gains_{};
    float preamp_db_{0.0F};
    bool enabled_{true};

    EqualizerPublication publication_;

    friend class EqualizerProcessor;
};

} // namespace wiola::audio
