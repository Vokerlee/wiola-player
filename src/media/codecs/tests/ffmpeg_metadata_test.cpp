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

#include <media/codecs/ffmpeg/composition.hpp>
#include <media/codecs/ffmpeg/metadata.hpp>

#include <media/local/resource.hpp>

#include <fixtures/temporary_directory.hpp>

#include <gtest/gtest.h>

extern "C" {
#include <libavformat/avformat.h>
}

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
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

std::filesystem::path tagged_copy(const std::filesystem::path& directory, const char* name,
    const Bytes& frames)
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

    const std::filesystem::path made{directory / name};
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

void append_be32(Bytes& out, std::uint32_t value)
{
    for (int shift = 24; shift >= 0; shift -= 8)
        out.push_back(static_cast<std::byte>((value >> shift) & 0xFFU));
}

std::filesystem::path flac_with_artwork(const std::filesystem::path& directory, const char* name,
    const Bytes& drawn)
{
    std::ifstream input{fixtures() / "tone.flac", std::ios::binary};
    const std::vector<char> encoded{std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{}};
    Bytes file;
    file.reserve(encoded.size());
    for (const char value : encoded)
        file.push_back(static_cast<std::byte>(static_cast<unsigned char>(value)));
    EXPECT_GE(file.size(), 4U);

    std::size_t offset = 4;
    while (offset + 4 <= file.size()) {
        const bool last = (file[offset] & std::byte{0x80}) != std::byte{};
        const std::size_t size = (std::to_integer<std::size_t>(file[offset + 1]) << 16U) |
            (std::to_integer<std::size_t>(file[offset + 2]) << 8U) |
            std::to_integer<std::size_t>(file[offset + 3]);

        if (last) {
            file[offset] &= std::byte{0x7F};
            offset += 4 + size;
            break;
        }

        offset += 4 + size;
    }

    Bytes picture;
    append_be32(picture, 3);
    append_be32(picture, 9);
    append(picture, "image/png");
    append_be32(picture, 0);
    append_be32(picture, 1);
    append_be32(picture, 1);
    append_be32(picture, 24);
    append_be32(picture, 0);
    append_be32(picture, static_cast<std::uint32_t>(drawn.size()));
    picture.insert(picture.end(), drawn.begin(), drawn.end());

    file.insert(file.begin() + static_cast<std::ptrdiff_t>(offset),
        {std::byte{0x86}, static_cast<std::byte>((picture.size() >> 16U) & 0xFFU),
            static_cast<std::byte>((picture.size() >> 8U) & 0xFFU),
            static_cast<std::byte>(picture.size() & 0xFFU)});
    file.insert(file.begin() + static_cast<std::ptrdiff_t>(offset + 4), picture.begin(),
        picture.end());

    const auto path = directory / name;
    std::ofstream output{path, std::ios::binary};
    output.write(reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));

    return path;
}

std::vector<std::vector<unsigned char>> packet_payloads(const std::filesystem::path& path)
{
    AVFormatContext* input = nullptr;
    if (avformat_open_input(&input, path.string().c_str(), nullptr, nullptr) < 0)
        return {};

    std::vector<std::vector<unsigned char>> payloads;
    AVPacket* packet = av_packet_alloc();

    while (av_read_frame(input, packet) >= 0) {
        payloads.emplace_back(packet->data, packet->data + packet->size);
        av_packet_unref(packet);
    }

    av_packet_free(&packet);
    avformat_close_input(&input);

    return payloads;
}

std::vector<float> decoded_samples(const std::filesystem::path& path)
{
    auto provider{wiola::codec::ffmpeg::make_provider()};
    auto asset = provider->open(wiola::media::local_resource(path));
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
    wiola::testing::TemporaryDirectory directory;
    const auto metadata{
        wiola::codec::ffmpeg::read_metadata(directory.path() / "no-such-track.mp3")};

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
    wiola::testing::TemporaryDirectory directory;
    const auto track{
        tagged_copy(directory.path(), "wiola_ffmpeg_tagged.mp3", title_frame("Together Forever"))};
    const auto metadata{wiola::codec::ffmpeg::read_metadata(track)};

    ASSERT_TRUE(metadata);
    EXPECT_EQ(metadata->track.title, "Together Forever");
}

TEST(FfmpegMetadata, ReadsExtendedFieldsAndReportsQualifiedMp3WriteSupport)
{
    wiola::testing::TemporaryDirectory directory;
    Bytes frames;
    append_frame(frames, title_frame("Extended"));
    append_frame(frames, text_frame("TCON", "Jazz"));
    append_frame(frames, text_frame("TYER", "2024"));
    append_frame(frames, comment_frame("Live recording"));
    append_frame(frames, text_frame("TCOM", "Composer"));
    append_frame(frames, text_frame("TRCK", "3/12"));
    append_frame(frames, text_frame("TPOS", "2/3"));
    const auto track{tagged_copy(directory.path(), "wiola_ffmpeg_extended.mp3", frames)};

    const auto metadata{wiola::codec::ffmpeg::read_metadata(track)};
    ASSERT_TRUE(metadata);
    EXPECT_EQ(metadata->track.genre, "Jazz");
    EXPECT_EQ(metadata->track.year, 2024);
    EXPECT_EQ(metadata->track.comment, "Live recording");
    EXPECT_EQ(metadata->track.composer, "Composer");
    EXPECT_EQ(metadata->track.track_number, 3);
    EXPECT_EQ(metadata->track.disc_number, 2);

    auto provider{wiola::codec::ffmpeg::make_provider()};
    auto asset{provider->open(wiola::media::local_resource(track))};
    ASSERT_TRUE(asset);
    EXPECT_TRUE((*asset)->metadata_write_capabilities().any());
}

TEST(FfmpegMetadata, DetectsMetadataWithoutAFileExtension)
{
    wiola::testing::TemporaryDirectory directory;
    const auto track{tagged_copy(directory.path(), "wiola_ffmpeg_tagged.whatever",
        title_frame("Together Forever"))};
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
    wiola::testing::TemporaryDirectory directory;
    constexpr char drawn_bytes[]{"\x89PNG\r\n\x1a\n and then some"};
    Bytes drawn;
    append(drawn, std::string_view{drawn_bytes, sizeof(drawn_bytes) - 1});
    const auto track{
        tagged_copy(directory.path(), "wiola_ffmpeg_with_cover.mp3", artwork_frame(drawn))};

    const auto metadata{wiola::codec::ffmpeg::read_metadata(track)};

    ASSERT_TRUE(metadata);
    EXPECT_EQ(metadata->artwork.mime_type, "image/png");
    ASSERT_EQ(metadata->artwork.data.size(), drawn.size());
    EXPECT_EQ(metadata->artwork.data, drawn);
}

TEST(FfmpegMetadata, QualifiedMp3WritingPreservesArtworkAndDecodedAudio)
{
    wiola::testing::TemporaryDirectory directory;
    constexpr char drawn_bytes[]{"\x89PNG\r\n\x1a\n and then some"};
    Bytes drawn;
    append(drawn, std::string_view{drawn_bytes, sizeof(drawn_bytes) - 1});

    const auto track =
        tagged_copy(directory.path(), "wiola_ffmpeg_write_cover.mp3", artwork_frame(drawn));
    const auto original_samples = decoded_samples(track);

    auto provider{wiola::codec::ffmpeg::make_provider()};
    auto asset = provider->open(wiola::media::local_resource(track));
    ASSERT_TRUE(asset);
    EXPECT_TRUE((*asset)->metadata_write_capabilities().any());

    wiola::media::MetadataWriteRequest request;
    request.overrides.title = "Updated title";
    request.overrides.comment = "Updated comment";

    const auto outcome = (*asset)->write_metadata(request);
    ASSERT_EQ(outcome.state, wiola::media::MetadataWriteState::written) << outcome.detail;

    const std::filesystem::path output = outcome.staged_output;

    const auto metadata = wiola::codec::ffmpeg::read_metadata(output);
    ASSERT_TRUE(metadata) << metadata.error().detail;
    EXPECT_EQ(metadata->track.title, "Updated title");
    EXPECT_EQ(metadata->track.comment, "Updated comment");
    EXPECT_EQ(metadata->artwork.mime_type, "image/png");
    EXPECT_EQ(metadata->artwork.data, drawn);
    EXPECT_EQ(decoded_samples(output), original_samples);
}

TEST(FfmpegMetadata, QualifiedFlacWritingPreservesCommentsPicturesAndPcm)
{
    wiola::testing::TemporaryDirectory directory;
    constexpr char drawn_bytes[]{"\x89PNG\r\n\x1a\n FLAC picture"};
    Bytes drawn;
    append(drawn, std::string_view{drawn_bytes, sizeof(drawn_bytes) - 1});

    const auto track = flac_with_artwork(directory.path(), "wiola_ffmpeg_write_cover.flac", drawn);
    const auto original_samples = decoded_samples(track);
    auto provider{wiola::codec::ffmpeg::make_provider()};

    auto asset = provider->open(wiola::media::local_resource(track));
    ASSERT_TRUE(asset);
    EXPECT_TRUE((*asset)->metadata_write_capabilities().any());

    wiola::media::MetadataWriteRequest first;
    first.overrides.comment = "Keep this comment";
    first.overrides.genre = "Jazz";
    auto outcome = (*asset)->write_metadata(first);
    ASSERT_EQ(outcome.state, wiola::media::MetadataWriteState::written) << outcome.detail;

    const std::filesystem::path commented = outcome.staged_output;

    asset = provider->open(wiola::media::local_resource(commented));
    ASSERT_TRUE(asset);
    wiola::media::MetadataWriteRequest second;
    second.overrides.title = "Updated FLAC";
    outcome = (*asset)->write_metadata(second);
    ASSERT_EQ(outcome.state, wiola::media::MetadataWriteState::written) << outcome.detail;

    const std::filesystem::path output = outcome.staged_output;
    const auto metadata = wiola::codec::ffmpeg::read_metadata(output);

    ASSERT_TRUE(metadata) << metadata.error().detail;
    EXPECT_EQ(metadata->track.title, "Updated FLAC");
    EXPECT_EQ(metadata->track.comment, "Keep this comment");
    EXPECT_EQ(metadata->track.genre, "Jazz");
    EXPECT_EQ(metadata->artwork.mime_type, "image/png");
    EXPECT_EQ(metadata->artwork.data, drawn);
    ASSERT_TRUE(metadata->track.duration);
    EXPECT_GT(*metadata->track.duration, wiola::units::Time{});
    EXPECT_EQ(decoded_samples(output), original_samples);
}

TEST(FfmpegMetadata, QualifiedMp4AndM4aWritingPreserveArtworkDurationAndPcm)
{
    wiola::testing::TemporaryDirectory directory;
    for (const char* name : {"attached-art.m4a", "tone-alac.mp4"}) {
        const auto track = directory.path() / name;
        std::filesystem::copy_file(fixtures() / name, track);
        const auto original_metadata = wiola::codec::ffmpeg::read_metadata(track);
        ASSERT_TRUE(original_metadata) << name;
        const auto original_samples = decoded_samples(track);

        auto provider{wiola::codec::ffmpeg::make_provider()};
        auto asset = provider->open(wiola::media::local_resource(track));
        ASSERT_TRUE(asset) << name;
        EXPECT_TRUE((*asset)->metadata_write_capabilities().any());

        wiola::media::MetadataWriteRequest request;
        request.overrides.title = "Updated MP4";
        request.overrides.composer = "Composer";
        const auto outcome = (*asset)->write_metadata(request);
        ASSERT_EQ(outcome.state, wiola::media::MetadataWriteState::written) << outcome.detail;

        const std::filesystem::path output = outcome.staged_output;

        const auto metadata = wiola::codec::ffmpeg::read_metadata(output);
        ASSERT_TRUE(metadata) << metadata.error().detail;
        EXPECT_EQ(metadata->track.title, "Updated MP4");
        EXPECT_EQ(metadata->track.composer, "Composer");
        EXPECT_EQ(metadata->artwork.data, original_metadata->artwork.data);
        EXPECT_EQ(metadata->artwork.mime_type, original_metadata->artwork.mime_type);
        EXPECT_EQ(metadata->track.duration, original_metadata->track.duration);
        EXPECT_EQ(decoded_samples(output), original_samples) << name;
    }
}

TEST(FfmpegMetadata, QualifiedOggAndOpusWritingPreservesPacketsAndChainedPcm)
{
    wiola::testing::TemporaryDirectory directory;
    for (const char* name : {"tone-vorbis.ogg", "tone.opus", "chained-same-shape.ogg"}) {
        const auto track = directory.path() / name;
        std::filesystem::copy_file(fixtures() / name, track);
        const auto original_metadata = wiola::codec::ffmpeg::read_metadata(track);
        ASSERT_TRUE(original_metadata) << name;
        const auto original_samples = decoded_samples(track);
        const auto original_packets = packet_payloads(track);

        auto provider{wiola::codec::ffmpeg::make_provider()};
        auto asset = provider->open(wiola::media::local_resource(track));
        ASSERT_TRUE(asset) << name;
        EXPECT_TRUE((*asset)->metadata_write_capabilities().any());

        wiola::media::MetadataWriteRequest request;
        request.overrides.title = "Updated Ogg";
        request.overrides.comment = "Preserved packet stream";
        const auto outcome = (*asset)->write_metadata(request);
        ASSERT_EQ(outcome.state, wiola::media::MetadataWriteState::written) << outcome.detail;

        const std::filesystem::path output = outcome.staged_output;

        const auto metadata = wiola::codec::ffmpeg::read_metadata(output);
        ASSERT_TRUE(metadata) << metadata.error().detail;
        EXPECT_EQ(metadata->track.title, "Updated Ogg");
        EXPECT_EQ(metadata->track.comment, "Preserved packet stream");
        EXPECT_EQ(packet_payloads(output), original_packets) << name;

        if (std::string_view{name} == "chained-same-shape.ogg") {
            ASSERT_TRUE(metadata->track.duration);
            EXPECT_GT(*metadata->track.duration, *original_metadata->track.duration);
        } else {
            EXPECT_EQ(metadata->track.duration, original_metadata->track.duration);
            EXPECT_EQ(decoded_samples(output), original_samples) << name;
        }
    }
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
    auto provider{wiola::codec::ffmpeg::make_provider()};
    auto asset{provider->open(wiola::media::local_resource(fixtures() / "tone.wav"))};
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
    wiola::testing::TemporaryDirectory directory;
    const std::filesystem::path invalid{directory.path() / "wiola_metadata_invalid.bin"};
    std::ofstream{invalid} << "not audio";

    auto unrecognized{wiola::codec::ffmpeg::read_metadata(invalid)};
    auto unsupported{wiola::codec::ffmpeg::read_metadata(fixtures() / "no-audio.mp4")};

    ASSERT_FALSE(unrecognized);
    ASSERT_FALSE(unsupported);
    EXPECT_EQ(unrecognized.error().category, wiola::media::ErrorCategory::not_mine);
    EXPECT_EQ(unsupported.error().category, wiola::media::ErrorCategory::unsupported);
}

} // namespace
