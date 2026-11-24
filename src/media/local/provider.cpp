/**
 * @file
 * @brief Local-file media provider composed from decoder and metadata adapters.
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

#include "provider.hpp"

#include <media/codecs/ffmpeg/metadata.hpp>
#include <media/codecs/ffmpeg/metadata_writer.hpp>
#include <media/local/file_stream.hpp>

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

    media::Result<media::MediaInfo> inspect(const media::OperationContext& context) override
    {
        if (context.stop_reason() != media::StopReason::none)
            return media::unexpected{context.error()};

        auto decoder{registry_.open(path_, context)};

        if (!decoder)
            return media::unexpected{decoder.error()};

        const pcm::StreamSpec spec{(*decoder)->spec()};
        const std::optional<pcm::Frames> length{(*decoder)->duration_frames()};
        return media::MediaInfo{.spec = spec,
            .duration =
                spec.valid() && length ? std::optional{spec.time_per(*length)} : std::nullopt,
            .seekable = (*decoder)->seekable()};
    }

    media::Result<media::Metadata> read_metadata(const media::OperationContext& context) override
    {
        if (context.stop_reason() != media::StopReason::none)
            return media::unexpected{context.error()};

        return ffmpeg::read_metadata(path_, context);
    }

    media::MetadataWriteCapabilities metadata_write_capabilities() const override
    {
        return registry_.metadata_write_capabilities(path_);
    }

    media::MetadataWriteOutcome write_metadata(const media::MetadataWriteRequest& request,
        const media::OperationContext& context) override
    {
        if (!metadata_write_capabilities().any())
            return {.state = media::MetadataWriteState::failed,
                .error = media::MetadataWriteError::unsupported,
                .source_modified = false,
                .detail = "this format is read-only"};

        auto name = path_.stem();
        name += ".wiola-writing";
        name += path_.extension();
        const auto temporary = path_.parent_path() / name;

        return ffmpeg::remux_metadata(path_, temporary, request, context);
    }

    media::Result<std::unique_ptr<media::AudioDecoder>>
    create_decoder(const media::OperationContext& context) override
    {
        if (context.stop_reason() != media::StopReason::none)
            return media::unexpected{context.error()};

        return registry_.open(path_, context);
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
LocalMediaProvider::open(const std::filesystem::path& path, const media::OperationContext& context)
{
    auto stream{media::LocalFileStream::open(path, context)};

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
