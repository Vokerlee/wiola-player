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

#pragma once

#include "playback.hpp"
#include "playhead.hpp"
#include "seek_plan.hpp"

#include <buffering/pcm_queue.hpp>
#include <wiola/audio/output.hpp>
#include <wiola/media/audio_decoder.hpp>

#include <span>

namespace wiola::engine {

/**
 * Executes a value plan immediately on the decoding thread; retains no collaborators.
 *
 * The caller quiesces output before a work plan. A failed seek commits the decoder's actual
 * preserved cursor. Only the claimed generation is acknowledged, so a newer request survives.
 * Priming checks cancellation/supersession before reading and before submitting completed PCM.
 */
[[nodiscard]] SeekOutcome execute_seek(const SeekPlan& plan, media::AudioDecoder& source,
    pcm::PcmQueue::Producer& buffer, const audio::Output& output, Playhead::Applier& applier,
    std::span<float> block, const Playback& playback);

} // namespace wiola::engine
