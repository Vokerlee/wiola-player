/**
 * @file
 * @brief Plays one decoder through a device.
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

#include "decode_loop.hpp"

#include <optional>
#include <span>
#include <stdexcept>
#include <utility>

namespace wiola::engine {

namespace {

std::vector<float> decode_block(pcm::StreamSpec spec, const TransportConfig& config)
{
    const std::optional<std::size_t> size{spec.checked_samples_per(config.decode_chunk)};

    if (!config.valid() || !size)
        throw std::invalid_argument{"invalid decode block configuration"};

    return std::vector<float>(*size);
}

} // namespace

DecodeLoop::DecodeLoop(std::unique_ptr<media::AudioDecoder> source, pcm::PcmQueue::Producer buffer,
    audio::Output& output, Playback& playback, Playhead::Applier applier, TransportWait& wait,
    const TransportConfig& config)
    : source_{std::move(source)}
    , buffer_{std::move(buffer)}
    , connection_{output}
    , output_{output}
    , playback_{playback}
    , applier_{std::move(applier)}
    , wait_{wait}
    , block_{decode_block(source_->spec(), config)}
    , seek_{*source_, buffer_, connection_.control(), output_, applier_, output_started_, block_}
{
}

void DecodeLoop::run(bool rewind) noexcept
{
    try {
        if (begin(rewind))
            decode();
        else
            playback_.finish(Playback::TerminalReason::faulted);
    } catch (...) {
        playback_.finish(Playback::TerminalReason::faulted);
    }

    stop_output();
    playback_.finish(Playback::TerminalReason::ended);
}

bool DecodeLoop::begin(bool rewind)
{
    static_cast<void>(seek_.begin(rewind));

    if (seek_.priming_failed())
        return false;

    if (!connection_.control().start())
        return false;

    output_started_ = true;

    return true;
}

void DecodeLoop::apply_seek()
{
    static_cast<void>(seek_.apply());

    if (seek_.priming_failed())
        playback_.finish(Playback::TerminalReason::faulted);

    // Whatever was asked for while the seek was being carried out.
    follow_playback();
}

void DecodeLoop::stop_output() noexcept
{
    if (!output_started_)
        return;

    connection_.control().stop();
    output_started_ = false;
}

void DecodeLoop::follow_playback()
{
    const bool wanted{playback_.playing()};

    if (wanted == output_started_)
        return;

    if (!wanted) {
        stop_output();
        return;
    }

    if (connection_.control().start()) {
        output_started_ = true;
        return;
    }

    // An output that will not start is a fault rather than a pause, and leaves nothing to play
    // through.
    playback_.finish(Playback::TerminalReason::faulted);
}

void DecodeLoop::decode()
{
    // What the last decode left, and how much of it has gone into the buffer. A block that would
    // not fit is kept here rather than waited out, so nothing below is ever held up by more than
    // one decode.
    std::size_t num_decoded{0};
    std::size_t num_handed{0};
    bool exhausted{false};

    // Any final state ends this: a listener's stop, a device that went away, or the end of the
    // track below.
    while (!playback_.finished()) {
        // Every turn, because a listener's thread only moves the state: this is the one place a
        // pause reaches the device, and the only one before the waits below.
        follow_playback();

        // A device that was started and is no longer running was not stopped by anything here.
        // Waiting on one that has gone away is how playback looks like it is still going with
        // nothing coming out of it.
        if (output_started_ && !output_.running()) {
            playback_.finish(Playback::TerminalReason::faulted);
            break;
        }

        if (applier_.seek_outstanding()) {
            apply_seek();

            // The block was decoded at the place that has just been left.
            num_decoded = 0;
            num_handed = 0;
            exhausted = false;
        }

        // Only once the last block has gone over in full, so that what would not fit is handed
        // over rather than decoded again.
        if (!exhausted && num_handed == num_decoded) {
            auto decoded{source_->read(block_)};

            if (!decoded) {
                playback_.finish(Playback::TerminalReason::faulted);
                break;
            }

            num_decoded = source_->spec().samples_per(decoded->frames);
            num_handed = 0;
            exhausted = decoded->end_of_stream;
        }

        if (exhausted && num_handed == num_decoded) {
            // Reaching the end of the source leaves what is buffered still to be heard; being
            // stopped does not, and being paused is neither - it waits to be resumed.
            if (played_out())
                break;

            wait_.wait();
            continue;
        }

        const std::size_t num_left{num_decoded - num_handed};
        const std::size_t num_taken{buffer_.push(std::span{block_}.subspan(num_handed, num_left))};

        // Both waits retain a bounded output-progress poll and wake immediately for commands.
        if (num_taken == 0) {
            wait_.wait();
            continue;
        }

        num_handed += num_taken;

        // The playhead counts what was handed over, not what was decoded: it is what the device
        // will play, and `played_out` above is measured against it.
        applier_.push(num_taken);
    }
}

bool DecodeLoop::played_out() const noexcept
{
    // A device that was never started, or that a pause has silenced, still has what is buffered
    // left to play.
    if (!output_started_)
        return false;

    return applier_.drained(output_.frames_played());
}

} // namespace wiola::engine
