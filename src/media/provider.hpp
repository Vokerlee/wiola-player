/**
 * @file
 * @brief Format-neutral access to an identified local media resource.
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

#pragma once

#include <wiola/media/audio_decoder.hpp>
#include <wiola/media/metadata.hpp>
#include <wiola/media/operation_context.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace wiola::media {

class MediaAsset {
public:
    MediaAsset(const MediaAsset&) = delete;
    MediaAsset& operator=(const MediaAsset&) = delete;
    MediaAsset(MediaAsset&&) = delete;
    MediaAsset& operator=(MediaAsset&&) = delete;

    virtual ~MediaAsset() = default;

    virtual Result<MediaInfo> inspect(const OperationContext& context = {}) = 0;
    virtual Result<Metadata> read_metadata(const OperationContext& context = {}) = 0;

    [[nodiscard]] virtual MetadataWriteCapabilities metadata_write_capabilities() const
    {
        return {};
    }

    virtual MetadataWriteOutcome write_metadata(const MetadataWriteRequest& request,
        const OperationContext& context = {})
    {
        static_cast<void>(request);
        static_cast<void>(context);

        return {.state = MetadataWriteState::failed,
            .error = MetadataWriteError::unsupported,
            .detail = "metadata writing is not supported for this media asset"};
    }

    virtual DecoderOpenResult create_decoder(const OperationContext& context = {}) = 0;

protected:
    MediaAsset() = default;
};

class MediaProvider {
public:
    MediaProvider(const MediaProvider&) = delete;
    MediaProvider& operator=(const MediaProvider&) = delete;
    MediaProvider(MediaProvider&&) = delete;
    MediaProvider& operator=(MediaProvider&&) = delete;

    virtual ~MediaProvider() = default;

    /// Identifies a resource without decoding it or creating playback/output state.
    virtual Result<std::unique_ptr<MediaAsset>> open(const std::filesystem::path& path,
        const OperationContext& context = {}) = 0;

    [[nodiscard]] virtual std::vector<std::string> playback_extensions() const = 0;

protected:
    MediaProvider() = default;
};

} // namespace wiola::media
