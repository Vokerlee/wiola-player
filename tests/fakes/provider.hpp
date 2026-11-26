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
#include <wiola/media/resource.hpp>

#include <functional>
#include <memory>
#include <utility>

namespace wiola::testing {

/// Test media supplied through the public driver port, without kernel-specific constructors.
class FakeMediaProvider final : public media::MediaProvider {
public:
    using AssetFactory = std::function<media::Result<std::unique_ptr<media::MediaAsset>>(
        const media::ResourceReference&, const media::OperationContext&
    )>;
    using DecoderFactory = std::function<media::DecoderOpenResult(const media::ResourceReference&,
        const media::OperationContext&)>;
    using MetadataRead =
        std::function<media::Result<media::Metadata>(const media::ResourceReference&,
            const media::OperationContext&)>;

    /// A supplied driver with no available assets, useful for idle control clients.
    [[nodiscard]] static std::shared_ptr<FakeMediaProvider> unavailable()
    {
        return std::make_shared<FakeMediaProvider>(
            [](
                const media::ResourceReference&, const media::OperationContext&
            ) -> media::Result<std::unique_ptr<media::MediaAsset>> {
                return media::unexpected{
                    media::Error{.category = media::ErrorCategory::unreadable,
                                 .detail = "test provider has no assets"}
                };
            }
        );
    }

    /// A decoder-only driver leaves metadata explicitly unsupported.
    [[nodiscard]] static std::shared_ptr<FakeMediaProvider> decoder_only(DecoderFactory decoder)
    {
        return std::make_shared<FakeMediaProvider>(std::move(decoder),
            [](const media::ResourceReference&,
                const media::OperationContext&) -> media::Result<media::Metadata> {
                return media::unexpected{
                    media::Error{.category = media::ErrorCategory::unsupported,
                                 .detail = "metadata is not supplied by this test"}
                };
            });
    }

    /// A metadata-only driver still uses the complete public asset contract.
    [[nodiscard]] static std::shared_ptr<FakeMediaProvider> metadata_only(MetadataRead metadata)
    {
        return std::make_shared<FakeMediaProvider>(
            [](const media::ResourceReference&,
                const media::OperationContext&) -> media::DecoderOpenResult {
                return media::unexpected{
                    media::Error{.category = media::ErrorCategory::unsupported,
                                 .detail = "decoding is not supplied by this test"}
                };
            },
            std::move(metadata)
        );
    }

    FakeMediaProvider(DecoderFactory decoder, MetadataRead metadata)
        : FakeMediaProvider{[callbacks = std::make_shared<const Callbacks>(std::move(decoder),
                                 std::move(metadata))](
                                const media::ResourceReference& resource,
                                const media::OperationContext&
                            ) -> media::Result<std::unique_ptr<media::MediaAsset>> {
            return std::unique_ptr<media::MediaAsset>{std::make_unique<Asset>(callbacks, resource)};
        }}
    {
    }

    /// Allows provider boundary failures without inventing another fake provider/asset pair.
    explicit FakeMediaProvider(AssetFactory open)
        : open_{std::move(open)}
    {
    }

    media::Result<std::unique_ptr<media::MediaAsset>> open(const media::ResourceReference& resource,
        const media::OperationContext& context) override
    {
        if (context.stop_reason() != media::StopReason::none)
            return media::unexpected{context.error()};
        return open_(resource, context);
    }

    std::vector<std::string> playback_extensions() const override { return {}; }

private:
    struct Callbacks {
        DecoderFactory decoder;
        MetadataRead metadata;
    };

    class Asset final : public media::MediaAsset {
    public:
        Asset(std::shared_ptr<const Callbacks> callbacks, media::ResourceReference resource)
            : callbacks_{std::move(callbacks)}
            , resource_{std::move(resource)}
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
            return callbacks_->metadata(resource_, context);
        }

        media::DecoderOpenResult create_decoder(const media::OperationContext& context) override
        {
            return callbacks_->decoder(resource_, context);
        }

    private:
        std::shared_ptr<const Callbacks> callbacks_;
        media::ResourceReference resource_;
    };

    AssetFactory open_;
};

} // namespace wiola::testing
