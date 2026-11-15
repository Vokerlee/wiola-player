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
#include <optional>
#include <span>

namespace wiola::codec {

enum class LengthAccuracy {
    exact,
    estimated,
    unknown,
};

struct DecoderStreamInfo {
    pcm::StreamSpec spec;
    std::optional<pcm::Frames> length;
    LengthAccuracy length_accuracy{LengthAccuracy::unknown};
    bool seekable{true};
};

struct DecodeOutcome {
    pcm::Frames frames;
    bool end_of_stream{false};
};

struct SeekOutcome {
    pcm::Frames target;
    pcm::Frames achieved;
    bool exact{true};
};

/**
 * Turns an audio stream into interleaved float frames on a worker-owned, blocking contract.
 *
 * A successful short read reports progress but does not by itself mean EOF. EOF is explicit, and
 * a backend/read failure is distinct from both. Exact lengths bound reads; estimated and unknown
 * lengths do not. Failed seeks leave the decoded position unchanged.
 */
class AudioDecoder {
public:
    NO_COPY_SEMANTIC(AudioDecoder);
    NO_MOVE_SEMANTIC(AudioDecoder);

    virtual ~AudioDecoder() = default;

    /// Writes only whole frames and reports frames written, EOF, or a typed failure.
    media::Result<DecodeOutcome> read(std::span<float> output);

    /// Sample-count convenience view for callers that do not need terminal status.
    media::Result<std::size_t> render(std::span<float> output);

    [[nodiscard]] const DecoderStreamInfo& stream_info() const noexcept { return stream_info_; }

    [[nodiscard]] pcm::StreamSpec spec() const noexcept { return stream_info_.spec; }

    [[nodiscard]] std::optional<pcm::Frames> duration_frames() const noexcept
    {
        return stream_info_.length;
    }

    [[nodiscard]] LengthAccuracy length_accuracy() const noexcept
    {
        return stream_info_.length_accuracy;
    }

    [[nodiscard]] bool seekable() const noexcept { return stream_info_.seekable; }

    [[nodiscard]] pcm::Frames position_frames() const noexcept { return position_; }

    [[nodiscard]] pcm::Frames num_frames() const noexcept
    {
        return stream_info_.length.value_or(pcm::Frames{});
    }

    [[nodiscard]] pcm::Frames num_frames_left() const noexcept
    {
        const pcm::Frames length{num_frames()};
        return position_ < length ? length - position_ : pcm::Frames{};
    }

    [[nodiscard]] bool exhausted() const noexcept { return exhausted_; }

    /// Reports the achieved frame, or a failure that leaves the decoded position unchanged.
    media::Result<SeekOutcome> seek(pcm::Frames frame_index);

protected:
    AudioDecoder(pcm::StreamSpec spec, pcm::Frames num_frames) noexcept
        : stream_info_{.spec = spec, .length = num_frames, .length_accuracy = LengthAccuracy::exact}
    {
    }

    explicit AudioDecoder(DecoderStreamInfo stream_info) noexcept
        : stream_info_{stream_info}
    {
    }

    /// Writes at most num_frames whole frames and reports actual exhaustion independently.
    virtual media::Result<DecodeOutcome> decode(std::span<float> output,
        pcm::Frames num_frames) = 0;

    /// Moves transactionally and reports where decoding will resume.
    virtual media::Result<SeekOutcome> seek_frame(pcm::Frames frame_index) = 0;

private:
    DecoderStreamInfo stream_info_;
    pcm::Frames position_{};
    bool exhausted_{false};
};

using Decoder = AudioDecoder;

} // namespace wiola::codec
