// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <wiola/media/artwork_reference.hpp>
#include <wiola/media/byte_stream.hpp>
#include <wiola/media/provider.hpp>
using namespace wiola;

class Decoder final : public media::AudioDecoder {
public:
    Decoder()
        : AudioDecoder{{}, pcm::Frames{1}}
    {
    }

private:
    media::Result<media::DecodeOutcome> decode(std::span<float> out, pcm::Frames n) override
    {
        std::fill_n(out.begin(), spec().samples_per(n), 0.5F);
        return media::DecodeOutcome{.frames = n};
    }

    media::Result<media::SeekOutcome> seek_frame(pcm::Frames n) override
    {
        return media::SeekOutcome{.target = n, .achieved = n};
    }
};

class Asset final : public media::MediaAsset {
public:
    media::Result<media::MediaInfo> inspect(const media::OperationContext&) override
    {
        return media::MediaInfo{};
    }

    media::Result<media::Metadata> read_metadata(const media::OperationContext&) override
    {
        return media::Metadata{};
    }

    media::DecoderOpenResult create_decoder(const media::OperationContext&) override
    {
        return std::unique_ptr<media::AudioDecoder>{std::make_unique<Decoder>()};
    }
};

class Provider final : public media::MediaProvider {
public:
    media::Result<std::unique_ptr<media::MediaAsset>> open(const std::filesystem::path&,
        const media::OperationContext&) override
    {
        return std::unique_ptr<media::MediaAsset>{std::make_unique<Asset>()};
    }

    std::vector<std::string> playback_extensions() const override { return {}; }
};

int main()
{
    const media::DecoderStreamInfo missing_exact_length{.spec = {},
        .length = std::nullopt,
        .length_accuracy = media::LengthAccuracy::exact};
    const auto rejected{missing_exact_length.validate()};
    if (rejected || rejected.error().category != media::ErrorCategory::damaged)
        return 4;

    Provider provider;
    auto asset = provider.open("controlled", {});
    if (!asset)
        return 1;
    auto opened = (*asset)->create_decoder();
    if (!opened)
        return 2;
    auto decoder = std::move(*opened);
    if (!decoder->stream_info().validate())
        return 5;
    (*asset).reset();
    std::array<float, 2> out{};
    auto read = decoder->read(out);
    if (!read || read->frames != pcm::Frames{1} || !read->end_of_stream || out[0] != 0.5F)
        return 3;
    auto seek = decoder->seek(pcm::Frames{});
    return !seek || decoder->exhausted();
}
