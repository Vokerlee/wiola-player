/**
 * @file
 * @brief Coherent seek requests, acknowledgements, and playback epoch publication.
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

#include "playback_epoch.hpp"

#include <cstddef>
#include <mutex>

namespace wiola::engine {

/**
 * Serializes any number of seek-request writers and one decoding-thread consumer.
 *
 * Request, claim, acknowledgement, and position reads take one short mutex, so no reader can pair
 * a request count with another request's target or observe half of a new epoch. Submit and drain
 * are worker-only and do not take the mutex. Nothing here is called by the audio callback.
 */
class SeekMailbox {
public:
    struct Claim {
        std::size_t requested{0};
        pcm::Frames target{};
        bool outstanding{false};
    };

    explicit SeekMailbox(std::size_t num_channels) noexcept;

    void request(pcm::Frames target) noexcept;
    [[nodiscard]] bool outstanding() const noexcept;
    [[nodiscard]] Claim claim() const noexcept;

    /// Commits a claimed position epoch. A newer request remains outstanding.
    void begin(pcm::Frames source_start, pcm::Frames output_baseline, const Claim& claim) noexcept;

    /// Worker-only submission and drain accounting for the current epoch.
    void submit(std::size_t num_samples) noexcept;
    [[nodiscard]] std::size_t num_pushed() const noexcept;
    [[nodiscard]] bool drained(pcm::Frames frames_played) const noexcept;

    /// A coherent requested or committed position for any reader.
    [[nodiscard]] pcm::Frames position(pcm::Frames frames_played) const noexcept;

private:
    mutable std::mutex mutex_;
    std::size_t requested_{0};
    std::size_t applied_{0};
    pcm::Frames target_{};
    PlaybackEpoch epoch_;
};

} // namespace wiola::engine
