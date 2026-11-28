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

namespace wiola::engine {

namespace {

SeekOutcome prime(media::AudioDecoder& source, pcm::PcmQueue::Producer& buffer,
    Playhead::Applier& applier, std::span<float> block, const Playback& playback,
    SeekOutcome settled)
{
    while (true) {
        if (const auto pending =
                SeekPlan::interruption(playback.finished(), applier.seek_outstanding()))
            return *pending;

        if (buffer.space_approx() < block.size())
            return settled;

        const auto decoded = source.read(block);

        if (const auto after_read =
                SeekPlan::interruption(playback.finished(), applier.seek_outstanding()))
            return *after_read;

        if (!decoded)
            return SeekOutcome::read_failed;

        if (decoded->frames == pcm::Frames{})
            return settled;

        const auto samples = source.spec().samples_per(decoded->frames);
        applier.push(buffer.push(block.first(samples)));
    }
}

} // namespace

SeekOutcome execute_seek(const SeekPlan& plan, media::AudioDecoder& source,
    pcm::PcmQueue::Producer& buffer, const audio::Output& output, Playhead::Applier& applier,
    std::span<float> block, const Playback& playback)
{
    if (plan.action() == SeekPlan::Action::none)
        return SeekOutcome::preserved;

    if (plan.action() == SeekPlan::Action::cancelled)
        return SeekOutcome::cancelled;

    auto settled = SeekOutcome::preserved;

    if (const auto target = plan.target()) {
        buffer.mark_discard();

        // The decoder owns validation and preserves its cursor on failure.
        if (source.seek(*target))
            settled = SeekOutcome::applied;
    }

    // A blocking seek may have completed after a stop or replacement. Acknowledge only
    // the claimed generation before checking whether its data should still be primed.
    applier.begin_at(source.position_frames(), output.frames_played(), plan.claim());

    return prime(source, buffer, applier, block, playback, settled);
}

} // namespace wiola::engine
