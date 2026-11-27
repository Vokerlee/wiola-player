/**
 * @file
 * @brief Reusable provider and decoder conformance checks.
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

#include <gtest/gtest.h>

#include <memory>
#include <vector>

namespace wiola::testing {

/// Supply a fresh finite fixture with the stated decoded length, independent of its format.
inline void verify_decoder_contract(media::AudioDecoder& decoder, pcm::Frames expected)
{
    ASSERT_TRUE(decoder.stream_info().validate());
    ASSERT_EQ(decoder.position_frames(), pcm::Frames{});
    const auto spec = decoder.spec();
    std::vector<float> block(spec.samples_per(pcm::Frames{17}));
    const auto empty = decoder.read({});
    ASSERT_TRUE(empty);
    EXPECT_EQ(empty->frames, pcm::Frames{});
    EXPECT_EQ(decoder.position_frames(), pcm::Frames{});

    std::size_t decoded = 0;
    while (!decoder.exhausted()) {
        const auto read = decoder.read(block);
        ASSERT_TRUE(read);
        ASSERT_LE(read->frames, pcm::Frames{17});
        ASSERT_TRUE(read->frames.count() > 0 || read->end_of_stream);
        decoded += read->frames.count();
        ASSERT_LE(decoded, expected.count());
        EXPECT_EQ(decoder.position_frames(), pcm::Frames{decoded});
    }
    EXPECT_EQ(decoded, expected.count());

    const auto ended = decoder.read(block);
    ASSERT_TRUE(ended);
    EXPECT_EQ(ended->frames, pcm::Frames{});
    EXPECT_TRUE(ended->end_of_stream);

    if (decoder.seekable()) {
        const auto seek = decoder.seek(pcm::Frames{});
        ASSERT_TRUE(seek);
        EXPECT_EQ(decoder.position_frames(), seek->achieved);
        EXPECT_EQ(decoder.exhausted(), expected == pcm::Frames{});
    }
}

/// Drop the only provider owner, then the asset, before exercising the owned decoder.
template<typename ProviderOwner>
void verify_provider_contract(ProviderOwner provider, const media::ResourceReference& resource,
    pcm::Frames expected)
{
    ASSERT_NE(provider, nullptr);
    auto opened = provider->open(resource, {});
    ASSERT_TRUE(opened);
    auto asset = std::move(*opened);
    ASSERT_NE(asset, nullptr);
    provider.reset();

    EXPECT_TRUE(asset->inspect());
    EXPECT_TRUE(asset->read_metadata());
    auto opened_decoder = asset->create_decoder();
    ASSERT_TRUE(opened_decoder);
    auto decoder = std::move(*opened_decoder);
    ASSERT_NE(decoder, nullptr);
    asset.reset();

    verify_decoder_contract(*decoder, expected);
}

} // namespace wiola::testing
