/**
 * @file
 * @brief FFmpeg metadata and provider integration tests.
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

Bytes text_frame(std::string_view id, std::string_view text)
{
    Bytes frame;
    append(frame, id);
    append(frame, {0, 0, 0, static_cast<unsigned>(text.size() + 1)});
    append(frame, {0, 0});
    append(frame, {0});
    append(frame, text);
    return frame;
}

Bytes title_frame(std::string_view title)
{
    return text_frame("TIT2", title);
}

Bytes comment_frame(std::string_view comment)
{
    Bytes frame;
    append(frame, "COMM");
    append(frame, {0, 0, 0, static_cast<unsigned>(comment.size() + 5)});
    append(frame, {0, 0});
    append(frame, {0});
    append(frame, "eng");
    append(frame, {0});
    append(frame, comment);
    return frame;
}

void append_frame(Bytes& out, const Bytes& frame)
{
    out.insert(out.end(), frame.begin(), frame.end());
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

std::vector<float> decoded_samples(const std::filesystem::path& path)
{
    wiola::codec::LocalMediaProvider provider;
    auto asset = provider.open(path);
    EXPECT_TRUE(asset);
    if (!asset)
        return {};

    auto decoder = (*asset)->create_decoder();
    EXPECT_TRUE(decoder);
    if (!decoder)
        return {};

    std::vector<float> samples;
    std::array<float, 1024> block{};

    for (int calls = 0; calls < 10000; ++calls) {
        auto read = (*decoder)->read(block);
        EXPECT_TRUE(read);
        if (!read)
            return samples;

        const std::size_t count = (*decoder)->spec().samples_per(read->frames);
        samples.insert(samples.end(), block.begin(),
            block.begin() + static_cast<std::ptrdiff_t>(count));

        if (read->end_of_stream)
            break;
    }

    return samples;
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

TEST(FfmpegMetadata, ReadsExtendedFieldsAndReportsQualifiedMp3WriteSupport)
{
    Bytes frames;
    append_frame(frames, title_frame("Extended"));
    append_frame(frames, text_frame("TCON", "Jazz"));
    append_frame(frames, text_frame("TYER", "2024"));
    append_frame(frames, comment_frame("Live recording"));
    append_frame(frames, text_frame("TCOM", "Composer"));
    append_frame(frames, text_frame("TRCK", "3/12"));
    append_frame(frames, text_frame("TPOS", "2/3"));
    const auto track{tagged_copy("wiola_ffmpeg_extended.mp3", frames)};

    const auto metadata{wiola::codec::ffmpeg::read_metadata(track)};
    ASSERT_TRUE(metadata);
    EXPECT_EQ(metadata->track.genre, "Jazz");
    EXPECT_EQ(metadata->track.year, 2024);
    EXPECT_EQ(metadata->track.comment, "Live recording");
    EXPECT_EQ(metadata->track.composer, "Composer");
    EXPECT_EQ(metadata->track.track_number, 3);
    EXPECT_EQ(metadata->track.disc_number, 2);

    wiola::codec::LocalMediaProvider provider;
    auto asset{provider.open(track)};
    ASSERT_TRUE(asset);
    EXPECT_TRUE((*asset)->metadata_write_capabilities().any());
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

TEST(FfmpegMetadata, QualifiedMp3WritingPreservesArtworkAndDecodedAudio)
{
    constexpr char drawn_bytes[]{"\x89PNG\r\n\x1a\n and then some"};
    Bytes drawn;
    append(drawn, std::string_view{drawn_bytes, sizeof(drawn_bytes) - 1});

    const auto track = tagged_copy("wiola_ffmpeg_write_cover.mp3", artwork_frame(drawn));
    const auto original_samples = decoded_samples(track);

    wiola::codec::LocalMediaProvider provider;
    auto asset = provider.open(track);
    ASSERT_TRUE(asset);
    EXPECT_TRUE((*asset)->metadata_write_capabilities().any());

    wiola::media::MetadataWriteRequest request;
    request.overrides.title = "Updated title";
    request.overrides.comment = "Updated comment";

    const auto outcome = (*asset)->write_metadata(request);
    ASSERT_EQ(outcome.state, wiola::media::MetadataWriteState::written) << outcome.detail;

    const std::filesystem::path output = outcome.detail;

    struct RemoveOutput {
        std::filesystem::path path;

        ~RemoveOutput() { std::filesystem::remove(path); }
    } cleanup{output};

    const auto metadata = wiola::codec::ffmpeg::read_metadata(output);
    ASSERT_TRUE(metadata) << metadata.error().detail;
    EXPECT_EQ(metadata->track.title, "Updated title");
    EXPECT_EQ(metadata->track.comment, "Updated comment");
    EXPECT_EQ(metadata->artwork.mime_type, "image/png");
    EXPECT_EQ(metadata->artwork.data, drawn);
    EXPECT_EQ(decoded_samples(output), original_samples);
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

TEST(FfmpegMetadata, KeepsArtworkBytesWhenTheImageCodecHasNoMimeMapping)
{
    AVFormatContext* format{avformat_alloc_context()};
    ASSERT_NE(format, nullptr);
    AVStream* stream{avformat_new_stream(format, nullptr)};
    ASSERT_NE(stream, nullptr);
    stream->disposition = AV_DISPOSITION_ATTACHED_PIC;
    std::uint8_t marker{42};
    stream->attached_pic.data = &marker;
    stream->attached_pic.size = 1;

    auto artwork{wiola::codec::ffmpeg::read_artwork(*stream)};

    ASSERT_FALSE(artwork.empty());
    EXPECT_TRUE(artwork.mime_type.empty());
    stream->attached_pic.data = nullptr;
    stream->attached_pic.size = 0;
    avformat_free_context(format);
}

TEST(FfmpegMetadata, SeparatesUnrecognizedInputFromUnsupportedAudio)
{
    const std::filesystem::path invalid{
        std::filesystem::temp_directory_path() / "wiola_metadata_invalid.bin"};
    std::ofstream{invalid} << "not audio";

    auto unrecognized{wiola::codec::ffmpeg::read_metadata(invalid)};
    auto unsupported{wiola::codec::ffmpeg::read_metadata(fixtures() / "no-audio.mp4")};

    ASSERT_FALSE(unrecognized);
    ASSERT_FALSE(unsupported);
    EXPECT_EQ(unrecognized.error().category, wiola::media::ErrorCategory::not_mine);
    EXPECT_EQ(unsupported.error().category, wiola::media::ErrorCategory::unsupported);
}

} // namespace
