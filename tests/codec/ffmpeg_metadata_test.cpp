/**
 * @file
 * @brief FFmpeg metadata fallback and provider composition tests.
 * @author Roman Glaz
 * @copyright © 2026, <vokerlee@gmail.com>
 */

#include <codec/backends/ffmpeg/metadata.hpp>
#include <codec/provider.hpp>

#include <gtest/gtest.h>

#include <array>
#include <filesystem>

namespace {

TEST(FfmpegMetadata, ReadsBoundedUnicodeFieldsDurationAndAttachedArtwork)
{
    auto metadata{wiola::codec::ffmpeg::read_metadata(std::filesystem::path{WIOLA_TEST_DATA_DIR} /
        "attached-art.m4a")};

    ASSERT_TRUE(metadata);
    EXPECT_EQ(metadata->track.title, "Тестовый трек");
    EXPECT_EQ(metadata->track.artist, "Артист");
    EXPECT_EQ(metadata->track.album, "Альбом");
    ASSERT_TRUE(metadata->track.duration);
    EXPECT_GT(*metadata->track.duration, wiola::units::Time{});
    EXPECT_EQ(metadata->artwork.mime_type, "image/png");
    EXPECT_FALSE(metadata->artwork.empty());
    EXPECT_LE(metadata->artwork.data.size(), wiola::media::max_encoded_artwork_bytes);
}

TEST(FfmpegMetadata, ExistingPlaybackDoesNotDependOnFallbackMetadata)
{
    wiola::codec::LocalMediaProvider provider;
    auto asset{provider.open(std::filesystem::path{WIOLA_TEST_DATA_DIR} / "tone.wav")};
    ASSERT_TRUE(asset);

    auto metadata{(*asset)->read_metadata()};
    ASSERT_TRUE(metadata);
    EXPECT_TRUE(metadata->track.title.empty());

    auto decoder{(*asset)->create_decoder()};
    ASSERT_TRUE(decoder);
    std::array<float, 8> samples{};
    EXPECT_GT((*decoder)->render(samples).value(), 0U);
}

} // namespace
