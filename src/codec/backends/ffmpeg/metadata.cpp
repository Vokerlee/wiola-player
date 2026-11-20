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

#include <media/local_file_stream.hpp>

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

constexpr std::size_t max_text_bytes{64 * 1024};

std::string field(AVDictionary* primary, AVDictionary* secondary, const char* name)
{
    const AVDictionaryEntry* entry{av_dict_get(primary, name, nullptr, 0)};
    if (!entry && secondary)
        entry = av_dict_get(secondary, name, nullptr, 0);
    if (!entry || !entry->value)
        return {};
    const std::size_t size{std::strlen(entry->value)};
    return size <= max_text_bytes ? std::string{entry->value, size} : std::string{};
}

std::string first_field(AVDictionary* primary, AVDictionary* secondary,
    std::initializer_list<const char*> names)
{
    for (const char* name : names) {
        std::string value = field(primary, secondary, name);
        if (!value.empty())
            return value;
    }

    return {};
}

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
    media::Metadata metadata;
    metadata.track.title = field(format->metadata, selected, "title");
    metadata.track.artist = field(format->metadata, selected, "artist");
    metadata.track.album = field(format->metadata, selected, "album");
    metadata.track.genre = first_field(format->metadata, selected, {"genre"});
    metadata.track.comment = first_field(format->metadata, selected, {"comment", "description"});
    metadata.track.composer = first_field(format->metadata, selected, {"composer"});
    metadata.track.year = number(first_field(format->metadata, selected, {"date", "year"}));
    metadata.track.track_number = number(first_field(format->metadata, selected, {"track"}));
    metadata.track.disc_number = number(first_field(format->metadata, selected, {"disc"}));

    if (facts->output.length)
        metadata.track.duration = facts->output.spec.time_per(*facts->output.length);
    read_artwork(metadata, format);
    return metadata;
}

} // namespace wiola::codec::ffmpeg
