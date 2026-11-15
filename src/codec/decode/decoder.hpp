/**
 * @file
 * @brief Backend-neutral audio decoding with explicit progress and terminal outcomes.
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

#include <core/macros.hpp>
#include <media/media.hpp>
#include <pcm/stream_spec.hpp>

#include <cstddef>
#include <span>

namespace wiola::codec {

struct DecodeOutcome {
    pcm::Frames frames;
    bool end_of_stream{false};
};

/**
 * Turns an audio stream into interleaved float frames on a worker-owned, blocking contract.
 *
 * A successful short read reports progress but does not by itself mean EOF. EOF is explicit, and
 * a backend/read failure is distinct from both. The current local-file readers expose exact frame
 * lengths and support seeking; failed seeks leave the decoded position unchanged.
 */
class AudioDecoder {
public:
    NO_COPY_SEMANTIC(AudioDecoder);
    NO_MOVE_SEMANTIC(AudioDecoder);

    virtual ~AudioDecoder() = default;

    /// Writes only whole frames and reports frames written, EOF, or a typed failure.
    media::Result<DecodeOutcome> read(std::span<float> output);

    /// Compatibility sample-count view for audio-source consumers during migration.
    media::Result<std::size_t> render(std::span<float> output);

    [[nodiscard]] pcm::StreamSpec spec() const noexcept { return spec_; }

    /// Exact total frames in the current local-file stream, and how many are still unread.
    [[nodiscard]] pcm::Frames num_frames() const noexcept { return num_frames_; }

    [[nodiscard]] pcm::Frames num_frames_left() const noexcept
    {
        return num_frames_ - num_frames_read_;
    }

    [[nodiscard]] bool exhausted() const noexcept { return num_frames_left() == pcm::Frames{}; }

    /// Moves to a frame or reports an invalid target/backend failure without changing position.
    media::Result<void> seek(pcm::Frames frame_index);

protected:
    AudioDecoder(pcm::StreamSpec spec, pcm::Frames num_frames) noexcept
        : spec_{spec}
        , num_frames_{num_frames}
    {
    }

    /// Writes at most num_frames whole frames; zero before the declared end is damage.
    virtual media::Result<pcm::Frames> decode(std::span<float> output, pcm::Frames num_frames) = 0;

    /// Moves the stream itself to frame_index. Never asked for a frame past the end.
    virtual bool seek_frame(pcm::Frames frame_index) = 0;

private:
    pcm::StreamSpec spec_;
    pcm::Frames num_frames_;
    pcm::Frames num_frames_read_{};
};

using Decoder = AudioDecoder;

} // namespace wiola::codec
