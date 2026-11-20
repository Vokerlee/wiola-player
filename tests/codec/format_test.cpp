/**
 * @file
 * @brief Unit tests for the leading bytes of a file.
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

#include <codec/decode/format.hpp>
#include <codec/decode/registry.hpp>

#include <media/local_file_stream.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string_view>

namespace {

using wiola::codec::FileHead;
using wiola::codec::Marker;

FileHead read_head(const std::filesystem::path& path)
{
    auto stream = wiola::media::LocalFileStream::open(path);

    if (!stream)
        return {};

    auto head = FileHead::read(**stream);
    return head ? *head : FileHead{};
}

/// Writes `bytes` to a file of its own and reads its head back.
FileHead head_of(const char* name, std::string_view bytes)
{
    const std::filesystem::path path{std::filesystem::temp_directory_path() / name};
    std::ofstream out{path, std::ios::binary};

    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    out.close();

    return read_head(path);
}

class ReadFailure final : public wiola::media::ByteStream {
public:
    wiola::media::Result<std::size_t> read(std::span<std::byte>,
        const wiola::media::OperationContext&) override
    {
        return wiola::media::unexpected{
            wiola::media::Error{.category = wiola::media::ErrorCategory::read_failed,
                                .detail = "injected header failure"}
        };
    }

    wiola::media::Result<void> seek(std::int64_t, wiola::media::SeekOrigin,
        const wiola::media::OperationContext&) override
    {
        return {};
    }

    wiola::media::Result<std::uint64_t> tell(const wiola::media::OperationContext&) override
    {
        return std::uint64_t{0};
    }
};

TEST(FileHead, PropagatesAReadFailure)
{
    ReadFailure stream;

    auto head{FileHead::read(stream)};

    ASSERT_FALSE(head);
    EXPECT_EQ(head.error().category, wiola::media::ErrorCategory::read_failed);
}

TEST(FileHead, IsEmptyWhenThereIsNoFile)
{
    const FileHead head{read_head("/nonexistent/wiola.wav")};

    EXPECT_TRUE(head.empty());
    EXPECT_FALSE(head.carries(Marker{0, "RIFF"}));
}

TEST(FileHead, IsEmptyForAnEmptyFile)
{
    EXPECT_TRUE(head_of("wiola_head_empty.bin", "").empty());
}

TEST(FileHead, IsEmptyForADirectory)
{
    EXPECT_TRUE(read_head(std::filesystem::temp_directory_path()).empty());
}

TEST(FileHead, CarriesAMarkerWhereItSaysItIs)
{
    const FileHead head{head_of("wiola_head_riff.bin", "RIFF\1\2\3\4WAVEfmt ")};

    EXPECT_FALSE(head.empty());
    EXPECT_TRUE(head.carries(Marker{0, "RIFF"}));
    EXPECT_TRUE(head.carries(Marker{8, "WAVE"}));
}

/// The offset is part of the marker: the same bytes elsewhere are not it.
TEST(FileHead, DoesNotCarryAMarkerAtAnotherOffset)
{
    const FileHead head{head_of("wiola_head_riff.bin", "RIFF\1\2\3\4WAVEfmt ")};

    EXPECT_FALSE(head.carries(Marker{4, "RIFF"}));
    EXPECT_FALSE(head.carries(Marker{0, "WAVE"}));
}

TEST(FileHead, CarriesNothingPastWhatWasRead)
{
    const FileHead head{head_of("wiola_head_short.bin", "RIFF")};

    EXPECT_FALSE(head.empty());
    EXPECT_TRUE(head.carries(Marker{0, "RIFF"}));

    // Beginning inside what was read is not enough; all of it has to be there.
    EXPECT_FALSE(head.carries(Marker{2, "FFxx"}));
    EXPECT_FALSE(head.carries(Marker{8, "WAVE"}));
}

/// Only the first bytes are kept, so a marker further in than that is never found.
TEST(FileHead, KeepsOnlyTheLeadingBytes)
{
    const FileHead head{head_of("wiola_head_long.bin", "0123456789abcdefghijklmnop")};

    EXPECT_TRUE(head.carries(Marker{12, "cdef"}));
    EXPECT_FALSE(head.carries(Marker{13, "defg"}));
    EXPECT_FALSE(head.carries(Marker{16, "ghij"}));
}

/// A signature is bytes, not text: the high ones compare as themselves.
TEST(FileHead, ComparesBytesRatherThanCharacters)
{
    const FileHead head{head_of("wiola_head_sync.bin", "\xFF\xFB\x90\x64rest")};

    EXPECT_TRUE(head.carries(Marker{0, "\xFF\xFB"}));
    EXPECT_FALSE(head.carries(Marker{0, "\x7F\x7B"}));
}

TEST(FormatWriteCapabilities, OnlyQualifiedFormatsExposeWritableFields)
{
    const wiola::codec::MediaBackendRegistry registry;
    std::size_t formats = 0;

    for (const wiola::codec::Backend& backend : registry.backends()) {
        for (const wiola::codec::Format& format : backend.formats) {
            ++formats;
            const bool qualified = format.name == "MPEG audio Layer III" || format.name == "FLAC" ||
                format.name == "MP4/M4A AAC-LC/ALAC" || format.name == "Ogg Vorbis/Opus";
            EXPECT_EQ(format.metadata_writes.any(), qualified) << format.name;
        }
    }

    EXPECT_GE(formats, 17U);

    for (const char* name :
        {"track.MP3", "track.FLAC", "track.M4A", "track.MP4", "track.OGG", "track.OPUS"}) {
        const auto capabilities = registry.metadata_write_capabilities(name);
        EXPECT_TRUE(capabilities.title && capabilities.artist && capabilities.album &&
            capabilities.genre && capabilities.year && capabilities.comment &&
            capabilities.composer && capabilities.track_number && capabilities.disc_number);
    }

    EXPECT_FALSE(registry.metadata_write_capabilities("track.mp2").any());
}

} // namespace
