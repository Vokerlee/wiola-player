/**
 * @file
 * @brief Owns transport commands and the thread that performs blocking playback work.
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

#include "decode_loop.hpp"
#include "playback.hpp"
#include "playhead.hpp"
#include "transport_wait.hpp"
#include <audio/device/output.hpp>
#include <core/macros.hpp>
#include <engine/transport/transport_config.hpp>
#include <functional>
#include <memory>
#include <pcm/pcm_queue.hpp>
#include <thread>
#include <wiola/media/audio_decoder.hpp>

namespace wiola::engine {

/**
 * Runs initial seek, priming, output start, and decoding on one worker thread.
 *
 * Commands only update transport/mailbox state, so callers do not wait for decoder or device I/O.
 * `start` reports request acceptance and thread-launch success; an eventual output failure is
 * reported as `faulted`. The worker is joined before its decoder and output connection are
 * destroyed.
 */
class TransportWorker {
public:
    using ThreadFactory = std::function<std::jthread(std::function<void()>)>;

    TransportWorker(std::unique_ptr<media::AudioDecoder> source, pcm::PcmQueue::Producer buffer,
        audio::Output& output, ThreadFactory launch = {}, TransportConfig config = {});

    NO_COPY_SEMANTIC(TransportWorker);
    NO_MOVE_SEMANTIC(TransportWorker);

    ~TransportWorker();

    /// Accepts a start from idle or a terminal state. False when already active or no thread
    /// could be launched. Output startup success is eventual and visible through `state`.
    [[nodiscard]] bool start();
    [[nodiscard]] bool pause() noexcept;
    [[nodiscard]] bool resume() noexcept;
    void stop() noexcept;
    void seek(pcm::Frames target) noexcept;

    void wait();

    [[nodiscard]] Playback::State state() const noexcept;
    [[nodiscard]] bool playing() const noexcept;
    [[nodiscard]] bool finished() const noexcept;
    [[nodiscard]] pcm::Frames position(pcm::Frames frames_played) const noexcept;

private:
    void work(bool rewind) noexcept;

    Playback playback_;
    Playhead head_;
    TransportWait wait_;
    DecodeLoop loop_;
    ThreadFactory launch_;
    std::jthread thread_;
};

} // namespace wiola::engine
