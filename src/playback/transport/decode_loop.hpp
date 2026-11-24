/**
 * @file
 * @brief Decodes a source into the buffer a device plays from, on a thread of its own.
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

#include "playhead.hpp"
#include "seek_operation.hpp"
#include "transport_wait.hpp"
#include <buffering/pcm_queue.hpp>
#include <transport/playback.hpp>
#include <wiola/audio/output.hpp>
#include <wiola/audio/output_connection.hpp>
#include <wiola/media/audio_decoder.hpp>
#include <wiola/playback/transport_config.hpp>

#include <memory>
#include <vector>

namespace wiola::engine {

/**
 * Keeps the buffer a device plays from fed, and keeps the device doing what playback says.
 *
 * The whole audio path is held here and nowhere else: the source, the end of the buffer that
 * fills it, the end of the output that drives it, and the end of the playhead that carries seeks
 * out. Whoever holds one of these has no way to reach any of them, which is what keeps a
 * listener's thread out of a device that this one is driving.
 *
 * Output claim acquisition belongs to the setup thread; run and everything it calls belong
 * to the transport worker thread.
 */
class DecodeLoop {
public:
    /// Takes the source it plays, the ends of the buffer, output and playhead it works through,
    /// the output it reads what has been heard from, and the playback it follows. All of them
    /// outlive it. Decode storage is allocated from the owned configuration at construction.
    DecodeLoop(std::unique_ptr<media::AudioDecoder> source, pcm::PcmQueue::Producer buffer,
        audio::Output& output, Playback& playback, Playhead::Applier applier, TransportWait& wait,
        const TransportConfig& config);

    DecodeLoop(const DecodeLoop&) = delete;
    DecodeLoop& operator=(const DecodeLoop&) = delete;
    DecodeLoop(DecodeLoop&&) = delete;
    DecodeLoop& operator=(DecodeLoop&&) = delete;
    ~DecodeLoop() = default;

    /// Setup thread, before launch and after any prior worker joined. A replacement candidate
    /// may have been constructed while the previous track still held the output claim.
    void acquire_output() noexcept { static_cast<void>(connection_.try_acquire()); }

    /// Runs initial seek, priming, output start and decoding on the worker thread.
    /// Every exit quiesces output and settles one terminal reason. Unexpected decoder failures
    /// become faulted without escaping the worker; an already accepted terminal reason wins.
    void run(bool rewind) noexcept;

private:
    void decode();

    /// Carries out a requested seek, marking what was decoded for the old place as stale.
    void apply_seek();

    /// Starts or stops the output so that it does what playback says.
    void follow_playback();

    /// Silences the output if it was started.
    void stop_output() noexcept;

    /// Whether the device has played everything handed to it, or has died trying. False while
    /// anything is still to be heard, which a pause counts as.
    [[nodiscard]] bool played_out() const noexcept;

    std::unique_ptr<media::AudioDecoder> source_;
    pcm::PcmQueue::Producer buffer_;
    audio::OutputConnection connection_;
    const audio::Output& output_;
    Playback& playback_;
    Playhead::Applier applier_;
    TransportWait& wait_;
    std::vector<float> block_;

    /// What the output was last asked for, owned exclusively by the worker execution.
    bool output_started_{false};
    SeekOperation seek_;
};

} // namespace wiola::engine
