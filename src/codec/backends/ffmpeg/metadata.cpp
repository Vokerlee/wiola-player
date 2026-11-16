/**
 * @file
 * @brief Private bounded FFmpeg metadata and attached-artwork fallback.
 * @author Roman Glaz
 * @copyright © 2026, <vokerlee@gmail.com>
 */

#include "metadata.hpp"

#include "input.hpp"

#include <media/local_file_stream.hpp>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
}

#include <cstring>

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

void read_artwork(media::Metadata& metadata, AVFormatContext* format)
{
    for (unsigned int index{0}; index < format->nb_streams; ++index) {
        const AVStream* stream{format->streams[index]};
        if (!(stream->disposition & AV_DISPOSITION_ATTACHED_PIC))
            continue;
        const AVPacket& picture{stream->attached_pic};
        if (!picture.data || picture.size <= 0 ||
            static_cast<std::size_t>(picture.size) > media::max_encoded_artwork_bytes)
            continue;
        const auto* bytes{reinterpret_cast<const std::byte*>(picture.data)};
        metadata.artwork.data.assign(bytes, bytes + picture.size);
        metadata.artwork.mime_type = stream->codecpar->codec_id == AV_CODEC_ID_PNG ? "image/png"
            : stream->codecpar->codec_id == AV_CODEC_ID_MJPEG                      ? "image/jpeg"
                                                                                   : "";
        return;
    }
}

} // namespace

media::Result<media::Metadata> read_metadata(const std::filesystem::path& path)
{
    auto stream{media::LocalFileStream::open(path)};
    if (!stream)
        return media::unexpected{stream.error()};
    auto input{FfmpegInput::open(std::move(*stream))};
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
    if (facts->output.length)
        metadata.track.duration = facts->output.spec.time_per(*facts->output.length);
    read_artwork(metadata, format);
    return metadata;
}

} // namespace wiola::codec::ffmpeg
