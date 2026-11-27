// SPDX-License-Identifier: GPL-3.0-or-later

#include <wiola/media/audio_decoder.hpp>
#include <wiola/media/provider.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <memory>

namespace {

class SampleDecoder final : public wiola::media::AudioDecoder {
public:
    SampleDecoder()
        : AudioDecoder{{}, wiola::pcm::Frames{2}}
    {
    }

private:
    wiola::media::Result<wiola::media::DecodeOutcome> decode(std::span<float> output,
        wiola::pcm::Frames frames) override
    {
        std::fill_n(output.begin(), spec().samples_per(frames), 0.25F);
        return wiola::media::DecodeOutcome{.frames = frames};
    }

    wiola::media::Result<wiola::media::SeekOutcome> seek_frame(wiola::pcm::Frames target) override
    {
        return wiola::media::SeekOutcome{.target = target, .achieved = target};
    }
};

class SampleAsset final : public wiola::media::MediaAsset {
public:
    wiola::media::Result<wiola::media::MediaInfo>
    inspect(const wiola::media::OperationContext&) override
    {
        return wiola::media::MediaInfo{};
    }

    wiola::media::Result<wiola::media::Metadata>
    read_metadata(const wiola::media::OperationContext&) override
    {
        return wiola::media::Metadata{};
    }

    wiola::media::DecoderOpenResult create_decoder(const wiola::media::OperationContext&) override
    {
        return std::unique_ptr<wiola::media::AudioDecoder>{std::make_unique<SampleDecoder>()};
    }
};

TEST(AudioDecoderPort, MediaAssetReturnsAnOwnedDecoderWithoutBackendServices)
{
    std::unique_ptr<wiola::media::MediaAsset> asset{std::make_unique<SampleAsset>()};
    auto opened{asset->create_decoder()};

    ASSERT_TRUE(opened);

    auto decoder{std::move(*opened)};

    asset.reset();

    std::array<float, 4> output{};
    const auto read{decoder->read(output)};

    ASSERT_TRUE(read);
    EXPECT_EQ(read->frames, wiola::pcm::Frames{2});
    EXPECT_TRUE(read->end_of_stream);
    EXPECT_EQ(output, (std::array{0.25F, 0.25F, 0.25F, 0.25F}));

    ASSERT_TRUE(decoder->seek(wiola::pcm::Frames{}));
    EXPECT_FALSE(decoder->exhausted());
}

} // namespace
