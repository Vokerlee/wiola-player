/**
 * @file
 * @brief FFmpeg metadata and provider integration tests.
 * @author Roman Glaz
 * @copyright © 2026, <vokerlee@gmail.com>
 */

#include <codec/backends/ffmpeg/metadata.hpp>
#include <codec/provider.hpp>

#include <gtest/gtest.h>

extern "C" {
#include <libavformat/avformat.h>
}

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;

const std::filesystem::path& fixtures()
{
    static const std::filesystem::path where{WIOLA_TEST_DATA_DIR};

    return where;
}

void append(Bytes& out, std::string_view text)
{
    for (const char letter : text)
        out.push_back(static_cast<std::byte>(letter));
}

void append(Bytes& out, std::initializer_list<unsigned> values)
{
    for (const unsigned value : values)
        out.push_back(static_cast<std::byte>(value));
}

std::filesystem::path tagged_copy(const char* name, const Bytes& frames)
{
    Bytes tag;
    append(tag, "ID3");
    append(tag, {3, 0, 0});
    append(tag,
        {static_cast<unsigned>((frames.size() >> 21U) & 0x7FU),
            static_cast<unsigned>((frames.size() >> 14U) & 0x7FU),
            static_cast<unsigned>((frames.size() >> 7U) & 0x7FU),
            static_cast<unsigned>(frames.size() & 0x7FU)});
    tag.insert(tag.end(), frames.begin(), frames.end());

    const std::filesystem::path made{std::filesystem::temp_directory_path() / name};
    std::ifstream sound{fixtures() / "tone.mp3", std::ios::binary};
    std::ofstream out{made, std::ios::binary};

    out.write(reinterpret_cast<const char*>(tag.data()), static_cast<std::streamsize>(tag.size()));
    out << sound.rdbuf();

    return made;
}

Bytes title_frame(std::string_view title)
{
    Bytes frame;
    append(frame, "TIT2");
    append(frame, {0, 0, 0, static_cast<unsigned>(title.size() + 1)});
    append(frame, {0, 0});
    append(frame, {0});
    append(frame, title);
    return frame;
}

Bytes artwork_frame(const Bytes& drawn)
{
    constexpr std::string_view mime{"image/png"};
    const std::size_t payload_size{1 + mime.size() + 1 + 1 + 1 + drawn.size()};

    Bytes frame;
    append(frame, "APIC");
    append(frame,
        {static_cast<unsigned>((payload_size >> 24U) & 0xFFU),
            static_cast<unsigned>((payload_size >> 16U) & 0xFFU),
            static_cast<unsigned>((payload_size >> 8U) & 0xFFU),
            static_cast<unsigned>(payload_size & 0xFFU)});
    append(frame, {0, 0});
    append(frame, {0});
    append(frame, mime);
    append(frame, {0, 3, 0});
    frame.insert(frame.end(), drawn.begin(), drawn.end());
    return frame;
}

TEST(FfmpegMetadata, RejectsAFileThatIsNotThere)
{
    const auto metadata{wiola::codec::ffmpeg::read_metadata(fixtures() / "no-such-track.mp3")};

    ASSERT_FALSE(metadata);
    EXPECT_EQ(metadata.error().category, wiola::media::ErrorCategory::unreadable);
}

TEST(FfmpegMetadata, ReadsDurationWithoutTextOrArtwork)
{
    const auto metadata{wiola::codec::ffmpeg::read_metadata(fixtures() / "tone.wav")};

    ASSERT_TRUE(metadata);
    EXPECT_TRUE(metadata->track.title.empty());
    EXPECT_TRUE(metadata->track.artist.empty());
    EXPECT_TRUE(metadata->track.album.empty());
    EXPECT_TRUE(metadata->artwork.empty());
    ASSERT_TRUE(metadata->track.duration.has_value());
    EXPECT_GT(*metadata->track.duration, wiola::units::Time{});
}

TEST(FfmpegMetadata, ReadsAnId3TitleFromMp3)
{
    const auto track{tagged_copy("wiola_ffmpeg_tagged.mp3", title_frame("Together Forever"))};
    const auto metadata{wiola::codec::ffmpeg::read_metadata(track)};

    ASSERT_TRUE(metadata);
    EXPECT_EQ(metadata->track.title, "Together Forever");
}

TEST(FfmpegMetadata, DetectsMetadataWithoutAFileExtension)
{
    const auto track{tagged_copy("wiola_ffmpeg_tagged.whatever", title_frame("Together Forever"))};
    const auto metadata{wiola::codec::ffmpeg::read_metadata(track)};

    ASSERT_TRUE(metadata);
    EXPECT_EQ(metadata->track.title, "Together Forever");
}

TEST(FfmpegMetadata, ReadsAFlacWithoutMetadataBlocks)
{
    const auto metadata{wiola::codec::ffmpeg::read_metadata(fixtures() / "tone.flac")};

    ASSERT_TRUE(metadata);
}

TEST(FfmpegMetadata, ReadsAnId3AttachedPicture)
{
    constexpr char drawn_bytes[]{"\x89PNG\r\n\x1a\n and then some"};
    Bytes drawn;
    append(drawn, std::string_view{drawn_bytes, sizeof(drawn_bytes) - 1});
    const auto track{tagged_copy("wiola_ffmpeg_with_cover.mp3", artwork_frame(drawn))};

    const auto metadata{wiola::codec::ffmpeg::read_metadata(track)};

    ASSERT_TRUE(metadata);
    EXPECT_EQ(metadata->artwork.mime_type, "image/png");
    ASSERT_EQ(metadata->artwork.data.size(), drawn.size());
    EXPECT_EQ(metadata->artwork.data, drawn);
}

TEST(FfmpegMetadata, ReadsBoundedUnicodeFieldsDurationAndAttachedArtwork)
{
    auto metadata{wiola::codec::ffmpeg::read_metadata(fixtures() / "attached-art.m4a")};

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

TEST(FfmpegMetadata, ReadingMetadataDoesNotPreventPlayback)
{
    wiola::codec::LocalMediaProvider provider;
    auto asset{provider.open(fixtures() / "tone.wav")};
    ASSERT_TRUE(asset);

    auto metadata{(*asset)->read_metadata()};
    ASSERT_TRUE(metadata);
    EXPECT_TRUE(metadata->track.title.empty());

    auto decoder{(*asset)->create_decoder()};
    ASSERT_TRUE(decoder);
    std::array<float, 8> samples{};
    EXPECT_GT((*decoder)->render(samples).value(), 0U);
}

TEST(FfmpegMetadata, DropsOverLimitTextWithoutDiscardingOtherFields)
{
    auto metadata{wiola::codec::ffmpeg::read_metadata(fixtures() / "large-metadata.ogg")};

    ASSERT_TRUE(metadata);
    EXPECT_TRUE(metadata->track.title.empty());
    EXPECT_EQ(metadata->track.artist, "Исполнитель");
}

TEST(FfmpegMetadata, RejectsOversizedArtworkBeforeCopyingItsPayload)
{
    AVFormatContext* format{avformat_alloc_context()};
    ASSERT_NE(format, nullptr);
    AVStream* stream{avformat_new_stream(format, nullptr)};
    ASSERT_NE(stream, nullptr);
    stream->disposition = AV_DISPOSITION_ATTACHED_PIC;
    std::uint8_t marker{0};
    stream->attached_pic.data = &marker;
    stream->attached_pic.size = static_cast<int>(wiola::media::max_encoded_artwork_bytes + 1);

    auto artwork{wiola::codec::ffmpeg::read_artwork(*stream)};

    EXPECT_TRUE(artwork.empty());
    stream->attached_pic.data = nullptr;
    stream->attached_pic.size = 0;
    avformat_free_context(format);
}

} // namespace
