/**
 * @file
 * @brief Controlled media provider using the same port as a production driver.
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

#include <wiola/media/provider.hpp>

#include <filesystem>
#include <functional>
#include <memory>
#include <utility>

namespace wiola::testing {

/// Test media supplied through the public driver port, without kernel-specific constructors.
class FakeMediaProvider final : public media::MediaProvider {
public:
    using AssetFactory = std::function<media::Result<std::unique_ptr<media::MediaAsset>>(
        const std::filesystem::path&, const media::OperationContext&
    )>;
    using DecoderFactory = std::function<media::DecoderOpenResult(const std::filesystem::path&,
        const media::OperationContext&)>;
    using MetadataRead =
        std::function<media::Result<media::Metadata>(const std::filesystem::path&)>;

    FakeMediaProvider(DecoderFactory decoder, MetadataRead metadata)
        : FakeMediaProvider{[callbacks = std::make_shared<const Callbacks>(std::move(decoder),
                                 std::move(metadata))](
                                const std::filesystem::path& path, const media::OperationContext&
                            ) -> media::Result<std::unique_ptr<media::MediaAsset>> {
            return std::unique_ptr<media::MediaAsset>{std::make_unique<Asset>(callbacks, path)};
        }}
    {
    }

    /// Allows provider boundary failures without inventing another fake provider/asset pair.
    explicit FakeMediaProvider(AssetFactory open)
        : open_{std::move(open)}
    {
    }

    media::Result<std::unique_ptr<media::MediaAsset>> open(const std::filesystem::path& path,
        const media::OperationContext& context) override
    {
        if (context.stop_reason() != media::StopReason::none)
            return media::unexpected{context.error()};
        return open_(path, context);
    }

    std::vector<std::string> playback_extensions() const override { return {}; }

private:
    struct Callbacks {
        DecoderFactory decoder;
        MetadataRead metadata;
    };

    class Asset final : public media::MediaAsset {
    public:
        Asset(std::shared_ptr<const Callbacks> callbacks, std::filesystem::path path)
            : callbacks_{std::move(callbacks)}
            , path_{std::move(path)}
        {
        }

        media::Result<media::MediaInfo> inspect(const media::OperationContext&) override
        {
            return media::unexpected{
                media::Error{media::ErrorCategory::unsupported,
                             "inspection is not supplied by this test"}
            };
        }

        media::Result<media::Metadata>
        read_metadata(const media::OperationContext& context) override
        {
            if (context.stop_reason() != media::StopReason::none)
                return media::unexpected{context.error()};
            return callbacks_->metadata(path_);
        }

        media::DecoderOpenResult create_decoder(const media::OperationContext& context) override
        {
            return callbacks_->decoder(path_, context);
        }

    private:
        std::shared_ptr<const Callbacks> callbacks_;
        std::filesystem::path path_;
    };

    AssetFactory open_;
};

} // namespace wiola::testing
