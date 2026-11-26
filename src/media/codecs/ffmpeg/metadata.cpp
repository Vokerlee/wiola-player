/**
 * @file
 * @brief Private bounded FFmpeg metadata and attached-artwork reader.
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

#include "metadata.hpp"

#include "input.hpp"
#include "metadata_text.hpp"

#include <wiola/media/local/file_stream.hpp>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
}

#include <charconv>
#include <cstring>
#include <initializer_list>
#include <utility>

namespace wiola::codec::ffmpeg {

namespace {

std::optional<std::uint32_t> number(const std::string& text)
{
    std::uint32_t value{0};
    const char* const last = text.data() + text.size();
    const auto parsed = std::from_chars(text.data(), last, value);

    if (parsed.ec != std::errc{} || parsed.ptr == text.data())
        return std::nullopt;

    if (parsed.ptr != last && *parsed.ptr != '/' && *parsed.ptr != '-')
        return std::nullopt;

    return value;
}

void read_artwork(media::Metadata& metadata, AVFormatContext* format)
{
    for (unsigned int index{0}; index < format->nb_streams; ++index) {
        media::Artwork artwork{wiola::codec::ffmpeg::read_artwork(*format->streams[index])};
        if (artwork.empty())
            continue;
        metadata.artwork = std::move(artwork);
        return;
    }
}

} // namespace

media::Artwork read_artwork(const AVStream& stream)
{
    media::Artwork artwork;
    if (!(stream.disposition & AV_DISPOSITION_ATTACHED_PIC))
        return artwork;
    const AVPacket& picture{stream.attached_pic};
    if (!picture.data || picture.size <= 0 ||
        static_cast<std::size_t>(picture.size) > media::max_encoded_artwork_bytes)
        return artwork;
    const auto* bytes{reinterpret_cast<const std::byte*>(picture.data)};
    artwork.data.assign(bytes, bytes + picture.size);
    artwork.mime_type = stream.codecpar->codec_id == AV_CODEC_ID_PNG ? "image/png"
        : stream.codecpar->codec_id == AV_CODEC_ID_MJPEG             ? "image/jpeg"
                                                                     : "";
    return artwork;
}

media::Result<media::Metadata> read_metadata(const std::filesystem::path& path,
    const media::OperationContext& context)
{
    auto stream{media::LocalFileStream::open(path, context)};
    if (!stream)
        return media::unexpected{stream.error()};
    auto input{FfmpegInput::open(std::move(*stream), context)};
    if (!input)
        return media::unexpected{input.error()};
    auto facts{(*input)->inspect()};
    if (!facts)
        return media::unexpected{facts.error()};

    AVFormatContext* format{(*input)->format()};
    AVDictionary* selected{format->streams[facts->stream_index]->metadata};
    const MetadataText text{format->metadata, selected,
        format->iformat && std::strcmp(format->iformat->name, "mp3") == 0};
    media::Metadata metadata;
    metadata.track.title = text.field("title");
    metadata.track.artist = text.field("artist");
    metadata.track.album = text.field("album");
    metadata.track.genre = text.first({"genre"});
    metadata.track.comment = text.first({"comment", "description"});
    metadata.track.composer = text.first({"composer"});
    metadata.track.year = number(text.first({"date", "year"}));
    metadata.track.track_number = number(text.first({"track"}));
    metadata.track.disc_number = number(text.first({"disc"}));

    if (facts->output.length)
        metadata.track.duration = facts->output.spec.time_per(*facts->output.length);
    read_artwork(metadata, format);
    return metadata;
}

} // namespace wiola::codec::ffmpeg
