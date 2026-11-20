/**
 * @file
 * @brief Transactional FFmpeg metadata remuxing without decoding or source replacement.
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

#include "metadata_writer.hpp"

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
#include <libavutil/error.h>
}

#include <array>
#include <memory>
#include <string>
#include <system_error>

namespace wiola::codec::ffmpeg {

namespace {

using Input = std::unique_ptr<AVFormatContext, void (*)(AVFormatContext*)>;
using Output = std::unique_ptr<AVFormatContext, void (*)(AVFormatContext*)>;
using Io = std::unique_ptr<AVIOContext, void (*)(AVIOContext*)>;
using Packet = std::unique_ptr<AVPacket, void (*)(AVPacket*)>;

int interrupted(void* opaque) noexcept
{
    const auto* context = static_cast<const media::OperationContext*>(opaque);
    return context->stop_reason() != media::StopReason::none;
}

std::string ffmpeg_error(int error)
{
    std::array<char, AV_ERROR_MAX_STRING_SIZE> text{};
    av_strerror(error, text.data(), text.size());

    return text.data();
}

media::MetadataWriteOutcome failed(media::MetadataWriteError error, std::string detail)
{
    return {.state = media::MetadataWriteState::failed,
        .error = error,
        .source_modified = false,
        .detail = std::move(detail)};
}

void set_text(AVDictionary*& metadata, const char* key, const std::optional<std::string>& value)
{
    if (value)
        av_dict_set(&metadata, key, value->c_str(), 0);
}

void set_number(AVDictionary*& metadata, const char* key, const std::optional<std::uint32_t>& value)
{
    if (value)
        av_dict_set(&metadata, key, std::to_string(*value).c_str(), 0);
}

void apply(AVDictionary*& metadata, const media::MetadataOverrides& edit)
{
    set_text(metadata, "title", edit.title);
    set_text(metadata, "artist", edit.artist);
    set_text(metadata, "album", edit.album);
    set_text(metadata, "genre", edit.genre);
    set_number(metadata, "date", edit.year);
    set_text(metadata, "comment", edit.comment);
    set_text(metadata, "composer", edit.composer);
    set_number(metadata, "track", edit.track_number);
    set_number(metadata, "disc", edit.disc_number);
}

} // namespace

media::MetadataWriteOutcome remux_metadata(const std::filesystem::path& source,
    const std::filesystem::path& temporary, const media::MetadataWriteRequest& request,
    const media::OperationContext& context)
{
    if (request.overrides.empty())
        return {.state = media::MetadataWriteState::unchanged,
            .error = media::MetadataWriteError::none,
            .source_modified = false,
            .detail = {}};

    if (source == temporary)
        return failed(media::MetadataWriteError::invalid_edit,
            "metadata output must be different from the source");

    std::error_code filesystem_error;
    if (std::filesystem::exists(temporary, filesystem_error))
        return failed(media::MetadataWriteError::invalid_output, "metadata output already exists");

    if (filesystem_error)
        return failed(media::MetadataWriteError::io, filesystem_error.message());

    AVFormatContext* raw_input{nullptr};
    if (context.stop_reason() != media::StopReason::none)
        return failed(media::MetadataWriteError::cancelled, context.error().detail);

    const int opened = avformat_open_input(&raw_input, source.string().c_str(), nullptr, nullptr);
    if (opened < 0)
        return failed(media::MetadataWriteError::io, ffmpeg_error(opened));

    Input input{raw_input, [](AVFormatContext* value) { avformat_close_input(&value); }};
    input->interrupt_callback = {.callback = interrupted,
        .opaque = const_cast<media::OperationContext*>(&context)};

    const int inspected = avformat_find_stream_info(input.get(), nullptr);
    if (inspected < 0)
        return failed(media::MetadataWriteError::invalid_output, ffmpeg_error(inspected));

    AVFormatContext* raw_output{nullptr};
    const int allocated =
        avformat_alloc_output_context2(&raw_output, nullptr, nullptr, temporary.string().c_str());
    if (allocated < 0 || raw_output == nullptr)
        return failed(media::MetadataWriteError::invalid_output,
            allocated < 0 ? ffmpeg_error(allocated) : "FFmpeg could not allocate output");

    Output output{raw_output, [](AVFormatContext* value) { avformat_free_context(value); }};
    output->interrupt_callback = input->interrupt_callback;
    av_dict_copy(&output->metadata, input->metadata, 0);
    apply(output->metadata, request.overrides);

    for (unsigned int index = 0; index < input->nb_streams; ++index) {
        AVStream* incoming = input->streams[index];
        AVStream* outgoing = avformat_new_stream(output.get(), nullptr);

        if (outgoing == nullptr)
            return failed(media::MetadataWriteError::invalid_output,
                "FFmpeg could not allocate an output stream");

        const int copied = avcodec_parameters_copy(outgoing->codecpar, incoming->codecpar);
        if (copied < 0)
            return failed(media::MetadataWriteError::invalid_output, ffmpeg_error(copied));

        outgoing->time_base = incoming->time_base;
        outgoing->disposition = incoming->disposition;
        av_dict_copy(&outgoing->metadata, incoming->metadata, 0);

        if (incoming->attached_pic.size > 0) {
            const int picture = av_packet_ref(&outgoing->attached_pic, &incoming->attached_pic);
            if (picture < 0)
                return failed(media::MetadataWriteError::invalid_output, ffmpeg_error(picture));
        }
    }

    AVIOContext* raw_io{nullptr};
    const int io_opened = avio_open(&raw_io, temporary.string().c_str(), AVIO_FLAG_WRITE);
    if (io_opened < 0)
        return failed(media::MetadataWriteError::io, ffmpeg_error(io_opened));

    Io io{raw_io, [](AVIOContext* value) { avio_closep(&value); }};
    output->pb = io.get();

    const int header = avformat_write_header(output.get(), nullptr);
    if (header < 0) {
        io.reset();
        std::filesystem::remove(temporary, filesystem_error);
        return failed(media::MetadataWriteError::invalid_output, ffmpeg_error(header));
    }

    Packet packet{av_packet_alloc(), [](AVPacket* value) { av_packet_free(&value); }};
    if (!packet) {
        io.reset();
        std::filesystem::remove(temporary, filesystem_error);
        return failed(media::MetadataWriteError::invalid_output,
            "FFmpeg could not allocate a packet");
    }

    while (true) {
        if (context.stop_reason() != media::StopReason::none) {
            io.reset();
            std::filesystem::remove(temporary, filesystem_error);
            return failed(media::MetadataWriteError::cancelled, context.error().detail);
        }

        const int read = av_read_frame(input.get(), packet.get());
        if (read == AVERROR_EOF)
            break;

        if (read < 0) {
            io.reset();
            std::filesystem::remove(temporary, filesystem_error);
            return failed(media::MetadataWriteError::io, ffmpeg_error(read));
        }

        AVStream* incoming = input->streams[packet->stream_index];
        AVStream* outgoing = output->streams[packet->stream_index];
        av_packet_rescale_ts(packet.get(), incoming->time_base, outgoing->time_base);

        const int written = av_interleaved_write_frame(output.get(), packet.get());
        av_packet_unref(packet.get());

        if (written < 0) {
            io.reset();
            std::filesystem::remove(temporary, filesystem_error);
            return failed(media::MetadataWriteError::io, ffmpeg_error(written));
        }
    }

    const int trailer = av_write_trailer(output.get());
    if (trailer < 0) {
        io.reset();
        std::filesystem::remove(temporary, filesystem_error);
        return failed(media::MetadataWriteError::io, ffmpeg_error(trailer));
    }

    io.reset();
    return {.state = media::MetadataWriteState::written,
        .error = media::MetadataWriteError::none,
        .source_modified = false,
        .detail = temporary.string()};
}

} // namespace wiola::codec::ffmpeg
