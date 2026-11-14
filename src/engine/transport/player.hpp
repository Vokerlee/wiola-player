/**
 * @file
 * @brief Plays one decoder through a device.
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

#include "transport_worker.hpp"
#include <audio/device/output.hpp>
#include <codec/decode/decoder.hpp>
#include <core/macros.hpp>
#include <engine/transport/playback.hpp>
#include <pcm/pcm_queue.hpp>
#include <pcm/stream_spec.hpp>
#include <utils/units.hpp>

#include <memory>

namespace wiola::engine {

/**
 * Plays one decoder, feeding it to a device from a thread of its own.
 *
 * Decoding and output startup may stall on a file or device, while the device callback wakes on
 * a deadline. The transport worker owns those operations, leaving the caller free to draw or ask
 * for another command. The worker is joined before the source is destroyed.
 */
class Player final {
public:
    /// Takes the source it plays, which must not be null, the buffer it decodes into, and the
    /// output that buffer is played through. Both outlive the player.
    Player(std::unique_ptr<codec::Decoder> source, pcm::PcmQueue::Producer buffer,
        audio::Output& output);

    NO_COPY_SEMANTIC(Player);
    NO_MOVE_SEMANTIC(Player);

    /// Stops playback and waits for the thread, so a player is never destroyed while it runs.
    ~Player();

    /// Accepts playback from the beginning or from a pending seek. A finished player is wound back
    /// by the worker. False when already active or no thread could be launched. Output startup
    /// failure is eventual and reported as `faulted` rather than as request acceptance.
    [[nodiscard]] bool start();

    /// Silences playback, keeping the position and everything already decoded, so that resuming
    /// is immediate. False when there was nothing playing to silence.
    [[nodiscard]] bool pause() noexcept;

    /// Plays on from where `pause` left off. False unless the player is paused. An output that
    /// will not start again ends playback as `faulted` on the worker rather than being reported
    /// as command refusal here.
    [[nodiscard]] bool resume() noexcept;

    /// Ends playback at once, without playing out what is already buffered. Does nothing if
    /// playback has already finished, and may be called from any thread.
    void stop() noexcept;

    /// Moves playback to `position`, measured from the start of the source. The worker applies it.
    /// A position beyond the end is ignored. Paused seeks stay paused, and a stopped or finished
    /// player holds the request for its next start.
    void seek(units::Time position) noexcept;

    /// Blocks until playback has ended.
    void wait();

    [[nodiscard]] Playback::State state() const noexcept;
    [[nodiscard]] bool playing() const noexcept;
    [[nodiscard]] bool finished() const noexcept;
    [[nodiscard]] units::Time total_time() const noexcept;

    /// Follows what is being heard rather than what has been decoded. A pending seek reports its
    /// target until the worker commits or recovers it.
    [[nodiscard]] units::Time time_played() const noexcept;

private:
    audio::Output& output_;
    pcm::StreamSpec spec_;
    pcm::Frames num_frames_;
    TransportWorker worker_;
};

} // namespace wiola::engine
