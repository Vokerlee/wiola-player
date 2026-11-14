/**
 * @file
 * @brief Playback position and submitted audio relative to one output baseline.
 * @author Roman Glaz
 * @copyright © 2026, <vokerlee@gmail.com>
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

#include <pcm/stream_spec.hpp>

#include <cstddef>

namespace wiola::engine {

/**
 * The frames submitted from one source position, measured against a monotonic output counter.
 *
 * Beginning and submitting belong to the decoding thread. Position may be read from another
 * thread through Playhead's publication protocol. Incomplete trailing samples are retained until
 * a later submission completes their frame.
 */
class PlaybackEpoch {
public:
    explicit PlaybackEpoch(std::size_t num_channels) noexcept;

    /// Starts a new epoch at a source position and the output's current monotonic count.
    void begin(pcm::Frames source_start, pcm::Frames output_baseline) noexcept;

    /// Counts newly submitted samples, retaining an incomplete frame for the next submission.
    void submit(std::size_t num_samples) noexcept;

    /// Complete frames submitted in this epoch.
    [[nodiscard]] pcm::Frames submitted() const noexcept;

    /// Samples submitted in this epoch, including an incomplete trailing frame.
    [[nodiscard]] std::size_t num_pushed() const noexcept;

    /// Whether the output has consumed every complete frame submitted in this epoch.
    [[nodiscard]] bool drained(pcm::Frames frames_played) const noexcept;

    /// The source position corresponding to the output's monotonic count.
    [[nodiscard]] pcm::Frames position(pcm::Frames frames_played) const noexcept;

private:
    [[nodiscard]] pcm::Frames consumed(pcm::Frames frames_played) const noexcept;

    std::size_t num_channels_;
    std::size_t source_start_{0};
    std::size_t output_baseline_{0};
    pcm::Frames submitted_{};
    std::size_t partial_samples_{0};
};

} // namespace wiola::engine
