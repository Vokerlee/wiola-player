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
#include <buffering/pcm_queue.hpp>
#include <wiola/audio/output.hpp>
#include <wiola/media/audio_decoder.hpp>

#include <optional>
#include <vector>

namespace wiola::engine {

/**
 * Carries out one claimed seek as a transaction on the decoding thread.
 *
 * Output is quiesced and stale buffered audio is invalidated before the backend is moved. Success
 * commits the requested epoch. Failure uses Decoder's preserved-position contract: the actual
 * cursor is committed instead, and the buffer is primed from there. The claimed generation is
 * acknowledged in either case, so a newer request remains outstanding.
 */
class SeekOperation {
public:
    enum class Outcome {
        applied,
        preserved,
        read_failed,
        cancelled,
        superseded,
    };

    SeekOperation(media::AudioDecoder& source, pcm::PcmQueue::Producer& buffer,
        audio::Output::Control& control, const audio::Output& output, Playhead::Applier& applier,
        bool& output_started, std::vector<float>& block, const Playback& playback) noexcept;

    SeekOperation(const SeekOperation&) = delete;
    SeekOperation& operator=(const SeekOperation&) = delete;
    SeekOperation(SeekOperation&&) = delete;
    SeekOperation& operator=(SeekOperation&&) = delete;

    /// Applies a pending request or replay rewind, then primes the committed epoch.
    [[nodiscard]] Outcome begin(bool rewind);

    /// No pending request is a no-op. A newer request or stop interrupts priming.
    [[nodiscard]] Outcome apply();

private:
    [[nodiscard]] Outcome reposition(const Playhead::Claim& claim, bool move);
    [[nodiscard]] Outcome prime(Outcome settled);
    [[nodiscard]] std::optional<Outcome> interruption() const noexcept;
    void quiesce() noexcept;

    media::AudioDecoder& source_;
    pcm::PcmQueue::Producer& buffer_;
    audio::Output::Control& control_;
    const audio::Output& output_;
    Playhead::Applier& applier_;
    bool& output_started_;
    std::vector<float>& block_;
    const Playback& playback_;
};

} // namespace wiola::engine
