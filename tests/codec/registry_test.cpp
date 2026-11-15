/**
 * @file
 * @brief Tests for deterministic, injectable media backend discovery.
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

#include <codec/decode/registry.hpp>

#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <fstream>

namespace {

class EmptyDecoder final : public wiola::codec::AudioDecoder {
public:
    EmptyDecoder()
        : AudioDecoder{{}, {}}
    {
    }

protected:
    wiola::media::Result<wiola::pcm::Frames> decode(std::span<float>, wiola::pcm::Frames) override
    {
        return wiola::pcm::Frames{};
    }

    bool seek_frame(wiola::pcm::Frames) override { return true; }
};

std::filesystem::path fake_file()
{
    const auto path{std::filesystem::temp_directory_path() / "wiola_registry.fake"};
    std::ofstream{path, std::ios::binary}.write("FAKE payload", 12);
    return path;
}

wiola::codec::Format fake_backend(int& attempts)
{
    static constexpr std::array markers{
        wiola::codec::Marker{0, "FAKE"}
    };
    static constexpr std::array<std::string_view, 1> extensions{".fake"};

    return {.name = "Fake",
        .markers = markers,
        .extensions = extensions,
        .open =
            [&attempts](const std::filesystem::path&) -> std::unique_ptr<wiola::codec::Decoder> {
            ++attempts;
            return std::make_unique<EmptyDecoder>();
        }};
}

TEST(MediaBackendRegistry, SelectsAnInjectedBackendByContent)
{
    int attempts{0};
    wiola::codec::MediaBackendRegistry registry{{fake_backend(attempts)}};

    auto decoder{registry.open(fake_file())};

    EXPECT_TRUE(decoder);
    EXPECT_EQ(attempts, 1);
}

TEST(MediaBackendRegistry, SeparatesPlaybackCapabilityFromDiscoveryMetadata)
{
    int attempts{0};
    auto backend{fake_backend(attempts)};
    backend.capabilities.playback = false;
    backend.capabilities.metadata = true;
    wiola::codec::MediaBackendRegistry registry{{backend}};

    auto decoder{registry.open(fake_file())};

    ASSERT_FALSE(decoder);
    EXPECT_EQ(decoder.error().category, wiola::media::ErrorCategory::unsupported);
    EXPECT_EQ(attempts, 0);
}

TEST(MediaBackendRegistry, BoundsFallbackAttempts)
{
    int first_attempts{0};
    int second_attempts{0};
    auto first{fake_backend(first_attempts)};
    first.open =
        [&first_attempts](const std::filesystem::path&) -> std::unique_ptr<wiola::codec::Decoder> {
        ++first_attempts;
        return nullptr;
    };
    wiola::codec::MediaBackendRegistry registry{
        {first, fake_backend(second_attempts)},
        1
    };

    auto decoder{registry.open(fake_file())};

    ASSERT_FALSE(decoder);
    EXPECT_EQ(first_attempts, 1);
    EXPECT_EQ(second_attempts, 0);
}

} // namespace
