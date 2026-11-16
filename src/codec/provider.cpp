/**
 * @file
 * @brief Local-file media provider composed from decoder and metadata adapters.
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

#include <codec/provider.hpp>

#include <codec/backends/taglib/tags.hpp>
#ifdef WIOLA_ENABLE_FFMPEG
#include <codec/backends/ffmpeg/metadata.hpp>
#endif
#include <media/local_file_stream.hpp>

#include <utility>

namespace wiola::codec {

namespace {

class LocalMediaAsset final : public media::MediaAsset {
public:
    LocalMediaAsset(std::filesystem::path path, MediaBackendRegistry registry)
        : path_{std::move(path)}
        , registry_{std::move(registry)}
    {
    }

    media::Result<media::MediaInfo> inspect() override
    {
        auto decoder{registry_.open(path_)};

        if (!decoder)
            return media::unexpected{decoder.error()};

        const pcm::StreamSpec spec{(*decoder)->spec()};
        const std::optional<pcm::Frames> length{(*decoder)->duration_frames()};
        return media::MediaInfo{.spec = spec,
            .duration =
                spec.valid() && length ? std::optional{spec.time_per(*length)} : std::nullopt,
            .seekable = (*decoder)->seekable()};
    }

    media::Result<media::Metadata> read_metadata() override
    {
        media::Metadata metadata{read_tags(path_)};
#ifdef WIOLA_ENABLE_FFMPEG
        auto fallback{ffmpeg::read_metadata(path_)};
        if (fallback) {
            if (metadata.track.title.empty())
                metadata.track.title = std::move(fallback->track.title);
            if (metadata.track.artist.empty())
                metadata.track.artist = std::move(fallback->track.artist);
            if (metadata.track.album.empty())
                metadata.track.album = std::move(fallback->track.album);
            if (!metadata.track.duration)
                metadata.track.duration = fallback->track.duration;
            if (metadata.artwork.empty())
                metadata.artwork = std::move(fallback->artwork);
        }
#endif
        return metadata;
    }

    media::Result<std::unique_ptr<Decoder>> create_decoder() override
    {
        return registry_.open(path_);
    }

private:
    std::filesystem::path path_;
    MediaBackendRegistry registry_;
};

} // namespace

LocalMediaProvider::LocalMediaProvider() = default;

LocalMediaProvider::LocalMediaProvider(MediaBackendRegistry registry)
    : registry_{std::move(registry)}
{
}

media::Result<std::unique_ptr<media::MediaAsset>>
LocalMediaProvider::open(const std::filesystem::path& path)
{
    auto stream{media::LocalFileStream::open(path)};

    if (!stream)
        return media::unexpected{stream.error()};

    std::unique_ptr<media::MediaAsset> asset{std::make_unique<LocalMediaAsset>(path, registry_)};
    return asset;
}

std::vector<std::string> LocalMediaProvider::playback_extensions() const
{
    return registry_.playback_extensions();
}

} // namespace wiola::codec
