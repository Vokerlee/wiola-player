/**
 * @file
 * @brief Backend-neutral audio decoding with explicit progress and terminal outcomes.
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

#include <wiola/media/audio_decoder.hpp>

#include <algorithm>

namespace wiola::media {

Result<void> AudioDecoder::validate_stream_info() const
{
    if (!spec().valid())
        return unexpected{
            Error{.category = ErrorCategory::damaged,
                  .detail = "decoder stream specification is invalid"}
        };

    if (length_accuracy() == LengthAccuracy::exact && !duration_frames())
        return unexpected{
            Error{.category = ErrorCategory::damaged, .detail = "exact decoder length is missing"}
        };

    return {};
}

Result<DecodeOutcome> AudioDecoder::read(std::span<float> output)
{
    const auto valid{validate_stream_info()};
    if (!valid)
        return unexpected{valid.error()};

    const pcm::StreamSpec stream_spec{spec()};

    if (output.size() % stream_spec.num_channels != 0)
        return unexpected{
            Error{.category = ErrorCategory::read_failed,
                  .detail = "decoder output does not contain whole frames"}
        };

    if (exhausted_)
        return DecodeOutcome{.frames = {}, .end_of_stream = true};

    pcm::Frames wanted{stream_spec.frames_per(output.size())};

    if (length_accuracy() == LengthAccuracy::exact)
        wanted = std::min(wanted, num_frames_left());

    if (wanted == pcm::Frames{}) {
        exhausted_ =
            length_accuracy() == LengthAccuracy::exact && num_frames_left() == pcm::Frames{};

        return DecodeOutcome{.frames = {}, .end_of_stream = exhausted_};
    }

    auto decoded{decode(output, wanted)};

    if (!decoded)
        return unexpected{decoded.error()};

    if (decoded->frames > wanted)
        return unexpected{
            Error{.category = ErrorCategory::damaged,
                  .detail = "decoder reported more frames than requested"}
        };

    if (decoded->frames == pcm::Frames{} && !decoded->end_of_stream)
        return unexpected{
            Error{.category = ErrorCategory::damaged,
                  .detail = "decoder made no progress and did not finish"}
        };

    position_ += decoded->frames;
    exhausted_ = decoded->end_of_stream ||
        (length_accuracy() == LengthAccuracy::exact && position_ == num_frames());

    return DecodeOutcome{.frames = decoded->frames, .end_of_stream = exhausted_};
}

Result<std::size_t> AudioDecoder::render(std::span<float> output)
{
    auto decoded{read(output)};

    if (!decoded)
        return unexpected{decoded.error()};

    return spec().samples_per(decoded->frames);
}

Result<SeekOutcome> AudioDecoder::seek(pcm::Frames frame_index)
{
    const auto valid{validate_stream_info()};
    if (!valid)
        return unexpected{valid.error()};

    if (!seekable())
        return unexpected{
            Error{.category = ErrorCategory::seek_failed,
                  .detail = "decoder stream is not seekable"}
        };

    if (length_accuracy() == LengthAccuracy::exact && frame_index > num_frames())
        return unexpected{
            Error{.category = ErrorCategory::invalid_seek,
                  .detail = "seek target is past the end"}
        };

    auto sought{seek_frame(frame_index)};
    if (!sought)
        return unexpected{sought.error()};

    if (sought->target != frame_index)
        return unexpected{
            Error{.category = ErrorCategory::seek_failed,
                  .detail = "decoder changed the seek target"}
        };

    if (sought->exact && sought->achieved != sought->target)
        return unexpected{
            Error{.category = ErrorCategory::seek_failed,
                  .detail = "decoder reported an inconsistent exact seek"}
        };

    if (length_accuracy() == LengthAccuracy::exact && sought->achieved > num_frames())
        return unexpected{
            Error{.category = ErrorCategory::seek_failed,
                  .detail = "decoder seek position is past the end"}
        };

    position_ = sought->achieved;
    exhausted_ = length_accuracy() == LengthAccuracy::exact && position_ == num_frames();
    return sought;
}

} // namespace wiola::media
