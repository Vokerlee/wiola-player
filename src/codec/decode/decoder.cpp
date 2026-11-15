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

#include <codec/decode/decoder.hpp>

#include <algorithm>

namespace wiola::codec {

media::Result<DecodeOutcome> AudioDecoder::read(std::span<float> output)
{
    if (output.size() % spec_.num_channels != 0)
        return media::unexpected{
            media::Error{.category = media::ErrorCategory::read_failed,
                         .detail = "decoder output does not contain whole frames"}
        };

    const pcm::Frames wanted{std::min(spec_.frames_per(output.size()), num_frames_left())};

    if (wanted == pcm::Frames{})
        return DecodeOutcome{.frames = {}, .end_of_stream = exhausted()};

    auto decoded{decode(output, wanted)};

    if (!decoded)
        return media::unexpected{decoded.error()};

    if (*decoded > wanted)
        return media::unexpected{
            media::Error{.category = media::ErrorCategory::damaged,
                         .detail = "decoder reported more frames than requested"}
        };

    if (*decoded == pcm::Frames{})
        return media::unexpected{
            media::Error{.category = media::ErrorCategory::damaged,
                         .detail = "decoder stopped before the declared end"}
        };

    num_frames_read_ += *decoded;
    return DecodeOutcome{.frames = *decoded, .end_of_stream = exhausted()};
}

media::Result<std::size_t> AudioDecoder::render(std::span<float> output)
{
    auto decoded{read(output)};

    if (!decoded)
        return media::unexpected{decoded.error()};

    return spec_.samples_per(decoded->frames);
}

media::Result<void> AudioDecoder::seek(pcm::Frames frame_index)
{
    if (frame_index > num_frames_)
        return media::unexpected{
            media::Error{.category = media::ErrorCategory::invalid_seek,
                         .detail = "seek target is past the end"}
        };

    if (!seek_frame(frame_index))
        return media::unexpected{
            media::Error{.category = media::ErrorCategory::seek_failed,
                         .detail = "decoder rejected the seek"}
        };

    num_frames_read_ = frame_index;
    return {};
}

} // namespace wiola::codec
