/**
 * @file
 * @brief A validated, recoverable seek and re-prime operation.
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

#include "seek_operation.hpp"

#include <span>

namespace wiola::engine {

SeekOperation::SeekOperation(media::AudioDecoder& source, pcm::PcmQueue::Producer& buffer,
    audio::Output::Control& control, const audio::Output& output, Playhead::Applier& applier,
    bool& output_started, std::vector<float>& block, const Playback& playback) noexcept
    : source_{source}
    , buffer_{buffer}
    , control_{control}
    , output_{output}
    , applier_{applier}
    , output_started_{output_started}
    , block_{block}
    , playback_{playback}
{
}

SeekOperation::Outcome SeekOperation::begin(bool rewind)
{
    const auto claim{applier_.claim()};
    return reposition(claim, rewind || claim.outstanding);
}

SeekOperation::Outcome SeekOperation::apply()
{
    const auto claim{applier_.claim()};
    return claim.outstanding ? reposition(claim, true) : Outcome::preserved;
}

SeekOperation::Outcome SeekOperation::reposition(const Playhead::Claim& claim, bool move)
{
    if (playback_.finished())
        return Outcome::cancelled;

    quiesce();

    if (move)
        buffer_.mark_discard();

    Outcome settled{Outcome::preserved};

    if (move) {
        const auto target{claim.outstanding ? claim.target : pcm::Frames{}};
        // The decoder owns seek validation and preserves its cursor on failure.
        if (source_.seek(target))
            settled = Outcome::applied;
    }

    // A blocking seek may have completed after a stop or replacement request. Its actual
    // decoder position is still acknowledged; only this claim is retired, never a newer one.
    applier_.begin_at(source_.position_frames(), output_.frames_played(), claim);

    return prime(settled);
}

std::optional<SeekOperation::Outcome> SeekOperation::interruption() const noexcept
{
    if (playback_.finished())
        return Outcome::cancelled;

    if (applier_.seek_outstanding())
        return Outcome::superseded;

    return std::nullopt;
}

void SeekOperation::quiesce() noexcept
{
    if (!output_started_)
        return;

    control_.stop();
    output_started_ = false;
}

SeekOperation::Outcome SeekOperation::prime(Outcome settled)
{
    while (true) {
        if (const auto pending{interruption()})
            return *pending;

        if (buffer_.space_approx() < block_.size())
            return settled;

        const auto decoded{source_.read(block_)};

        if (const auto after_read{interruption()})
            return *after_read;

        if (!decoded)
            return Outcome::read_failed;

        if (decoded->frames == pcm::Frames{})
            return settled;

        const auto samples{source_.spec().samples_per(decoded->frames)};
        applier_.push(buffer_.push(std::span{block_}.first(samples)));
    }
}

} // namespace wiola::engine
