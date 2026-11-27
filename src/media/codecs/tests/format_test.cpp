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

#include <fakes/byte_stream.hpp>
#include <wiola/media/codecs/format.hpp>
#include <wiola/media/codecs/registry.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <limits>
#include <span>
#include <string_view>
#include <vector>

namespace {

using wiola::codec::FileHead;
using wiola::codec::Marker;

FileHead head_of(std::string_view bytes)
{
    const auto raw{std::as_bytes(std::span{bytes.data(), bytes.size()})};
    wiola::testing::MemoryByteStream stream{
        std::vector<std::byte>{raw.begin(), raw.end()}
    };
    const auto head{FileHead::read(stream)};

    EXPECT_TRUE(head);

    return head ? *head : FileHead{};
}

TEST(FileHead, PropagatesAReadFailure)
{
    wiola::testing::MemoryByteStream stream;
    stream.fail_reads();

    auto head{FileHead::read(stream)};

    ASSERT_FALSE(head);
    EXPECT_EQ(head.error().category, wiola::media::ErrorCategory::read_failed);
}

TEST(FileHead, IsEmptyForAnEmptyFile)
{
    EXPECT_TRUE(head_of("").empty());
}

TEST(FileHead, CarriesAMarkerWhereItSaysItIs)
{
    const FileHead head{head_of("RIFF\1\2\3\4WAVEfmt ")};

    EXPECT_FALSE(head.empty());
    EXPECT_TRUE(head.carries(Marker{0, "RIFF"}));
    EXPECT_TRUE(head.carries(Marker{8, "WAVE"}));
}

/// The offset is part of the marker: the same bytes elsewhere are not it.
TEST(FileHead, DoesNotCarryAMarkerAtAnotherOffset)
{
    const FileHead head{head_of("RIFF\1\2\3\4WAVEfmt ")};

    EXPECT_FALSE(head.carries(Marker{4, "RIFF"}));
    EXPECT_FALSE(head.carries(Marker{0, "WAVE"}));
}

TEST(FileHead, CarriesNothingPastWhatWasRead)
{
    const FileHead head{head_of("RIFF")};

    EXPECT_FALSE(head.empty());
    EXPECT_TRUE(head.carries(Marker{0, "RIFF"}));

    // Beginning inside what was read is not enough; all of it has to be there.
    EXPECT_FALSE(head.carries(Marker{2, "FFxx"}));
    EXPECT_FALSE(head.carries(Marker{8, "WAVE"}));
}

TEST(FileHead, RejectsOverflowingMarkerRanges)
{
    const auto head{head_of("RIFF")};
    const auto maximum{std::numeric_limits<std::size_t>::max()};

    EXPECT_FALSE(head.carries(Marker{maximum, "RIFF"}));
    EXPECT_FALSE(head.carries(Marker{maximum - 1, "RIFF"}));
    EXPECT_FALSE(head.carries(Marker{maximum - 2, "RIFF"}));
}

TEST(FileHead, EmptyMarkersStillRequireAValidPosition)
{
    const auto head{head_of("RIFF")};

    EXPECT_TRUE(head.carries(Marker{0, ""}));
    EXPECT_TRUE(head.carries(Marker{4, ""}));
    EXPECT_FALSE(head.carries(Marker{5, ""}));

    const FileHead empty;

    EXPECT_TRUE(empty.carries(Marker{0, ""}));
    EXPECT_FALSE(empty.carries(Marker{1, ""}));
}

/// Only the first bytes are kept, so a marker further in than that is never found.
TEST(FileHead, KeepsOnlyTheLeadingBytes)
{
    const FileHead head{head_of("0123456789abcdefghijklmnop")};

    EXPECT_TRUE(head.carries(Marker{12, "cdef"}));
    EXPECT_FALSE(head.carries(Marker{13, "defg"}));
    EXPECT_FALSE(head.carries(Marker{16, "ghij"}));
}

/// A signature is bytes, not text: the high ones compare as themselves.
TEST(FileHead, ComparesBytesRatherThanCharacters)
{
    const FileHead head{head_of("\xFF\xFB\x90\x64rest")};

    EXPECT_TRUE(head.carries(Marker{0, "\xFF\xFB"}));
    EXPECT_FALSE(head.carries(Marker{0, "\x7F\x7B"}));
}

} // namespace
