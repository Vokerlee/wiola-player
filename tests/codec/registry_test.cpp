/**
 * @file
 * @brief Tests for deterministic, injectable media backend discovery.
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

#include <codec/decode/registry.hpp>

#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace {

class EmptyDecoder final : public wiola::codec::AudioDecoder {
public:
    EmptyDecoder()
        : AudioDecoder{{}, {}}
    {
    }

protected:
    wiola::media::Result<wiola::codec::DecodeOutcome> decode(std::span<float>,
        wiola::pcm::Frames) override
    {
        return wiola::codec::DecodeOutcome{.frames = {}, .end_of_stream = true};
    }

    wiola::media::Result<wiola::codec::SeekOutcome>
    seek_frame(wiola::pcm::Frames frame_index) override
    {
        return wiola::codec::SeekOutcome{.target = frame_index, .achieved = frame_index};
    }
};

std::filesystem::path fake_file()
{
    const auto path{std::filesystem::temp_directory_path() / "wiola_registry.fake"};
    std::ofstream{path, std::ios::binary}.write("FAKE payload", 12);
    return path;
}

wiola::codec::Format fake_backend(int& attempts)
{
    static const std::array markers{
        wiola::codec::Marker{0, "FAKE"}
    };
    static constexpr std::array<std::string_view, 1> extensions{".fake"};

    return {"Fake", markers, extensions,
        [&attempts](const std::filesystem::path&) -> wiola::codec::Opened {
            ++attempts;
            std::unique_ptr<wiola::codec::Decoder> decoder{std::make_unique<EmptyDecoder>()};
            return decoder;
        }};
}

TEST(MediaBackendRegistry, RejectsAZeroAttemptBudget)
{
    EXPECT_THROW((wiola::codec::MediaBackendRegistry{{}, 0}), std::invalid_argument);
}

TEST(MediaBackendRegistry, PropagatesAHeaderReadFailure)
{
    if (!std::filesystem::exists("/proc/self/mem"))
        GTEST_SKIP() << "requires a procfs file that opens but cannot be read";

    int attempts{0};
    const wiola::codec::MediaBackendRegistry registry{{fake_backend(attempts)}};

    auto decoder{registry.open("/proc/self/mem")};

    ASSERT_FALSE(decoder);
    EXPECT_EQ(decoder.error().category, wiola::media::ErrorCategory::read_failed);
    EXPECT_EQ(attempts, 0);
}

TEST(MediaBackendRegistry, StopsImmediatelyWhenAReadableResourceFails)
{
    int first_attempts{0};
    int second_attempts{0};
    auto first{fake_backend(first_attempts)};
    first.open = [&first_attempts](const std::filesystem::path&) -> wiola::codec::Opened {
        ++first_attempts;

        return wiola::media::unexpected{
            wiola::media::Error{.category = wiola::media::ErrorCategory::unreadable,
                                .detail = "resource disappeared"}
        };
    };
    const wiola::codec::MediaBackendRegistry registry{
        {first, fake_backend(second_attempts)}
    };

    auto decoder{registry.open(fake_file())};

    ASSERT_FALSE(decoder);
    EXPECT_EQ(decoder.error().category, wiola::media::ErrorCategory::unreadable);
    EXPECT_EQ(first_attempts, 1);
    EXPECT_EQ(second_attempts, 0);
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
    first.open = [&first_attempts](const std::filesystem::path&) -> wiola::codec::Opened {
        ++first_attempts;
        return wiola::media::unexpected{
            wiola::media::Error{.category = wiola::media::ErrorCategory::not_mine,
                                .detail = "not fake"}
        };
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

TEST(MediaBackendRegistry, DescriptorCopiesOwnTheirIdentityAndProbeData)
{
    int attempts{0};
    auto backend{[&attempts] {
        std::string name{"Dynamically qualified fake"};
        std::string signature{"FAKE"};
        std::string extension{".fake"};
        const std::array markers{
            wiola::codec::Marker{0, signature}
        };
        const std::array<std::string_view, 1> extensions{extension};
        return wiola::codec::Format{name, markers, extensions,
            [&attempts](const std::filesystem::path&) -> wiola::codec::Opened {
                ++attempts;
                std::unique_ptr<wiola::codec::Decoder> decoder{std::make_unique<EmptyDecoder>()};
                return decoder;
            }};
    }()};
    const wiola::codec::MediaBackendRegistry registry{{backend}};

    EXPECT_EQ(registry.backends().front().name, "Dynamically qualified fake");
    EXPECT_EQ(registry.backends().front().formats.front().extensions.front(), ".fake");
    EXPECT_TRUE(registry.open(fake_file()));
    EXPECT_EQ(attempts, 1);
}

TEST(MediaBackendRegistry, RecognizedUnsupportedEncodingIsNotReportedAsDamage)
{
    int attempts{0};
    auto backend{fake_backend(attempts)};
    backend.open = [](const std::filesystem::path&) -> wiola::codec::Opened {
        return wiola::media::unexpected{
            wiola::media::Error{.category = wiola::media::ErrorCategory::unsupported,
                                .detail = "recognized container has an unsupported codec"}
        };
    };
    const wiola::codec::MediaBackendRegistry registry{{backend}};

    auto decoder{registry.open(fake_file())};

    ASSERT_FALSE(decoder);
    EXPECT_EQ(decoder.error().category, wiola::media::ErrorCategory::unsupported);
    EXPECT_EQ(decoder.error().detail, "recognized container has an unsupported codec");
}

TEST(MediaBackendRegistry, MultipleFormatsShareOneBackendProbe)
{
    int attempts{0};
    const std::array formats{fake_backend(attempts), fake_backend(attempts)};
    const wiola::codec::Backend backend{"Combined fake", formats,
        [&attempts](const std::filesystem::path&) -> wiola::codec::Opened {
            ++attempts;
            return wiola::media::unexpected{
                wiola::media::Error{.category = wiola::media::ErrorCategory::not_mine,
                                    .detail = "not fake"}
            };
        }};
    const wiola::codec::MediaBackendRegistry registry{{backend}};

    auto decoder{registry.open(fake_file())};

    ASSERT_FALSE(decoder);
    EXPECT_EQ(attempts, 1);
}

TEST(MediaBackendRegistry, CancellationIsObservableBeforeBackendOpening)
{
    int attempts{0};
    const wiola::codec::MediaBackendRegistry registry{{fake_backend(attempts)}};
    std::stop_source cancelled;
    cancelled.request_stop();

    auto decoder{registry.open(fake_file(), cancelled.get_token())};

    ASSERT_FALSE(decoder);
    EXPECT_EQ(decoder.error().category, wiola::media::ErrorCategory::cancelled);
    EXPECT_EQ(attempts, 0);
}

TEST(MediaBackendRegistry, CapabilityViewDeduplicatesAndExcludesMetadataOnlyBackends)
{
    int attempts{0};
    auto metadata_only{fake_backend(attempts)};
    metadata_only.capabilities.playback = false;
    const wiola::codec::MediaBackendRegistry registry{
        {fake_backend(attempts), fake_backend(attempts), metadata_only}
    };

    EXPECT_EQ(registry.playback_extensions(), std::vector<std::string>{".fake"});
}

} // namespace
