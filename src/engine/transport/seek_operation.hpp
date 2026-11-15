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

#pragma once

#include "playhead.hpp"
#include <audio/device/output.hpp>
#include <codec/decode/decoder.hpp>
#include <core/macros.hpp>
#include <pcm/pcm_queue.hpp>

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
    SeekOperation(codec::Decoder& source, pcm::PcmQueue::Producer& buffer,
        audio::Output::Control& control, const audio::Output& output, Playhead::Applier& applier,
        bool& output_started, std::vector<float>& block) noexcept;

    NO_COPY_SEMANTIC(SeekOperation);
    NO_MOVE_SEMANTIC(SeekOperation);

    /// Applies a seek held from before playback, or rewinds when `rewind` is set, then primes.
    /// False when the backend refused and the preserved cursor was committed instead.
    [[nodiscard]] bool begin(bool rewind);

    /// Quiesces output, applies the latest claimed seek, and primes the resulting epoch.
    [[nodiscard]] bool apply();

    [[nodiscard]] bool priming_failed() const noexcept { return priming_failed_; }

private:
    [[nodiscard]] pcm::Frames current_frame() const noexcept;
    void quiesce() noexcept;
    void prime();

    codec::Decoder& source_;
    pcm::PcmQueue::Producer& buffer_;
    audio::Output::Control& control_;
    const audio::Output& output_;
    Playhead::Applier& applier_;
    bool& output_started_;
    std::vector<float>& block_;
    bool priming_failed_{false};
};

} // namespace wiola::engine
