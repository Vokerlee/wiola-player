/**
 * @file
 * @brief A validated, recoverable seek and re-prime operation.
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

#include "seek_operation.hpp"

#include <engine/tuning.hpp>

#include <array>
#include <span>

namespace wiola::engine {

SeekOperation::SeekOperation(codec::Decoder& source, pcm::PcmQueue::Producer& buffer,
    audio::Output::Control& control, const audio::Output& output, Playhead::Applier& applier,
    bool& output_started) noexcept
    : source_{source}
    , buffer_{buffer}
    , control_{control}
    , output_{output}
    , applier_{applier}
    , output_started_{output_started}
{
}

bool SeekOperation::begin(bool rewind)
{
    const Playhead::Claim claim{applier_.claim()};
    const pcm::Frames target{claim.outstanding ? claim.target : pcm::Frames{}};
    const bool move{rewind || claim.outstanding};

    quiesce();

    if (move)
        buffer_.mark_discard();

    const bool sought{!move || (target <= source_.num_frames() && source_.seek(target))};
    applier_.begin_at(sought ? target : current_frame(), output_.frames_played(), claim);
    prime();

    return sought;
}

bool SeekOperation::apply()
{
    const Playhead::Claim claim{applier_.claim()};

    quiesce();
    buffer_.mark_discard();

    const bool sought{
        claim.outstanding && claim.target <= source_.num_frames() && source_.seek(claim.target)};
    applier_.begin_at(sought ? claim.target : current_frame(), output_.frames_played(), claim);
    prime();

    return sought;
}

pcm::Frames SeekOperation::current_frame() const noexcept
{
    return source_.num_frames() - source_.num_frames_left();
}

void SeekOperation::quiesce() noexcept
{
    if (!output_started_)
        return;

    control_.stop();
    output_started_ = false;
}

void SeekOperation::prime()
{
    std::array<float, tuning::decode_chunk_samples> block{};

    while (buffer_.space_approx() >= block.size()) {
        const std::size_t num_decoded{source_.render(block)};

        if (num_decoded == 0)
            break;

        applier_.push(buffer_.push(std::span{block}.first(num_decoded)));
    }
}

} // namespace wiola::engine
