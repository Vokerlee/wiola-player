// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <stop_token>
#include <wiola/media/artwork_reference.hpp>
#include <wiola/media/byte_stream.hpp>
#include <wiola/media/provider.hpp>
using namespace wiola;

class ForwardBytes final : public media::ByteStream {
private:
    media::Result<std::size_t> read_impl(std::span<std::byte> out,
        const media::OperationContext&) override
    {
        if (consumed_)
            return std::size_t{};
        out.front() = std::byte{0x2A};
        consumed_ = true;
        return std::size_t{1};
    }

    media::Result<void> seek_impl(std::int64_t, media::SeekOrigin,
        const media::OperationContext&) override
    {
        return media::unexpected{
            media::Error{media::ErrorCategory::unsupported, "forward-only consumer"}
        };
    }

    media::Result<std::uint64_t> tell_impl(const media::OperationContext&) override
    {
        return std::uint64_t{consumed_ ? 1U : 0U};
    }

    bool consumed_{false};
};

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
    ForwardBytes stream;
    std::array<std::byte, 2> bytes{};
    const auto bytes_read{stream.read(bytes)};
    const auto cursor{stream.tell()};
    const auto unsupported{stream.seek(0, media::SeekOrigin::start)};
    if (!bytes_read || *bytes_read != 1 || bytes.front() != std::byte{0x2A} || !cursor ||
        *cursor != 1 || unsupported ||
        unsupported.error().category != media::ErrorCategory::unsupported)
        return 6;
    std::stop_source stopped;
    stopped.request_stop();
    const auto rejected_read{stream.read(bytes, {.stop = stopped.get_token(), .deadline = {}})};
    if (rejected_read || rejected_read.error().category != media::ErrorCategory::cancelled)
        return 7;

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
