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

#include "playback_epoch.hpp"

namespace wiola::engine {

PlaybackEpoch::PlaybackEpoch(std::size_t num_channels) noexcept
    : num_channels_{num_channels}
{
}

void PlaybackEpoch::begin(pcm::Frames source_start, pcm::Frames output_baseline) noexcept
{
    source_start_.store(source_start.count(), std::memory_order_relaxed);
    output_baseline_.store(output_baseline.count(), std::memory_order_relaxed);
    submitted_ = pcm::Frames{};
    partial_samples_ = 0;
}

void PlaybackEpoch::submit(std::size_t num_samples) noexcept
{
    const std::size_t total{partial_samples_ + num_samples};
    submitted_ += pcm::Frames{total / num_channels_};
    partial_samples_ = total % num_channels_;
}

pcm::Frames PlaybackEpoch::submitted() const noexcept
{
    return submitted_;
}

std::size_t PlaybackEpoch::num_pushed() const noexcept
{
    return submitted_.count() * num_channels_ + partial_samples_;
}

bool PlaybackEpoch::drained(pcm::Frames frames_played) const noexcept
{
    return consumed(frames_played) >= submitted_;
}

pcm::Frames PlaybackEpoch::position(pcm::Frames frames_played) const noexcept
{
    return pcm::Frames{source_start_.load(std::memory_order_acquire)} + consumed(frames_played);
}

pcm::Frames PlaybackEpoch::consumed(pcm::Frames frames_played) const noexcept
{
    const pcm::Frames baseline{output_baseline_.load(std::memory_order_acquire)};

    return frames_played > baseline ? frames_played - baseline : pcm::Frames{};
}

} // namespace wiola::engine
