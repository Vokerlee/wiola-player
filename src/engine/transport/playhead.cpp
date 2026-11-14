/**
 * @file
 * @brief Where playback is, and where it has been asked to go.
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

#include "playhead.hpp"

namespace wiola::engine {

Playhead::Playhead(std::size_t num_channels) noexcept
    : seeks_{num_channels}
{
}

void Playhead::request_seek(pcm::Frames frame_index) noexcept
{
    seeks_.request(frame_index);
}

bool Playhead::seek_outstanding() const noexcept
{
    return seeks_.outstanding();
}

Playhead::Claim Playhead::claim() const noexcept
{
    return seeks_.claim();
}

void Playhead::begin_at(pcm::Frames frame_index, pcm::Frames frames_played,
    const Claim& claim) noexcept
{
    seeks_.begin(frame_index, frames_played, claim);
}

void Playhead::push(std::size_t num_samples) noexcept
{
    seeks_.submit(num_samples);
}

std::size_t Playhead::num_pushed() const noexcept
{
    return seeks_.num_pushed();
}

bool Playhead::drained(pcm::Frames frames_played) const noexcept
{
    return seeks_.drained(frames_played);
}

pcm::Frames Playhead::position(pcm::Frames frames_played) const noexcept
{
    return seeks_.position(frames_played);
}

} // namespace wiola::engine
