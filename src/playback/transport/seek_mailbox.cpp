/**
 * @file
 * @brief Coherent seek requests, acknowledgements, and playback epoch publication.
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

#include "seek_mailbox.hpp"

namespace wiola::engine {

SeekMailbox::SeekMailbox(std::size_t num_channels) noexcept
    : epoch_{num_channels}
{
}

void SeekMailbox::request(pcm::Frames target) noexcept
{
    const std::lock_guard lock{mutex_};
    target_ = target;
    ++requested_;
}

bool SeekMailbox::outstanding() const noexcept
{
    const std::lock_guard lock{mutex_};
    return requested_ != applied_;
}

SeekMailbox::Claim SeekMailbox::claim() const noexcept
{
    const std::lock_guard lock{mutex_};
    return Claim{
        .requested = requested_,
        .target = target_,
        .outstanding = requested_ != applied_,
    };
}

void SeekMailbox::begin(pcm::Frames source_start, pcm::Frames output_baseline,
    const Claim& claim) noexcept
{
    const std::lock_guard lock{mutex_};
    epoch_.begin(source_start, output_baseline);
    applied_ = claim.requested;
}

void SeekMailbox::submit(std::size_t num_samples) noexcept
{
    epoch_.submit(num_samples);
}

std::size_t SeekMailbox::num_pushed() const noexcept
{
    return epoch_.num_pushed();
}

bool SeekMailbox::drained(pcm::Frames frames_played) const noexcept
{
    return epoch_.drained(frames_played);
}

pcm::Frames SeekMailbox::position(pcm::Frames frames_played) const noexcept
{
    const std::lock_guard lock{mutex_};

    if (requested_ != applied_)
        return target_;

    return epoch_.position(frames_played);
}

} // namespace wiola::engine
