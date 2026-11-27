/**
 * @file
 * @brief Format-neutral access to an identified media resource.
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

#include "provider_contract.hpp"

#include <wiola/media/audio_decoder.hpp>
#include <wiola/media/provider.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <vector>

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

class SampleProvider final : public wiola::media::MediaProvider {
public:
    explicit SampleProvider(bool& destroyed)
        : destroyed_{destroyed}
    {
    }

    ~SampleProvider() override { destroyed_ = true; }

    wiola::media::Result<std::unique_ptr<wiola::media::MediaAsset>>
    open(const wiola::media::ResourceReference&, const wiola::media::OperationContext&) override
    {
        return std::unique_ptr<wiola::media::MediaAsset>{std::make_unique<SampleAsset>()};
    }

    std::vector<std::string> playback_extensions() const override { return {"sample"}; }

private:
    bool& destroyed_;
};

TEST(AudioDecoderPort, AnOpenedAssetOutlivesItsProvider)
{
    bool destroyed = false;
    std::unique_ptr<wiola::media::MediaProvider> provider =
        std::make_unique<SampleProvider>(destroyed);
    const auto opened = provider->open({"sample", "track", "Track"});

    ASSERT_TRUE(opened);
    EXPECT_EQ(provider->playback_extensions(), (std::vector<std::string>{"sample"}));

    provider.reset();

    EXPECT_TRUE(destroyed);
    EXPECT_TRUE((*opened)->inspect());
    EXPECT_TRUE((*opened)->read_metadata());
    EXPECT_TRUE((*opened)->create_decoder());
}

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

TEST(AudioDecoderPort, SatisfiesTheReusableProviderAndDecoderContract)
{
    bool destroyed = false;
    wiola::testing::verify_provider_contract(std::make_unique<SampleProvider>(destroyed),
        wiola::media::ResourceReference{"sample", "track", "Track"}, wiola::pcm::Frames{2});
    EXPECT_TRUE(destroyed);
}

} // namespace
