/**
 * @file
 * @brief Local file byte-stream integration contracts.
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

#include <fixtures/temporary_directory.hpp>
#include <media/local/file_stream.hpp>

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stop_token>
#include <type_traits>
#include <utility>

namespace {

using wiola::media::ErrorCategory;
using wiola::media::SeekOrigin;

static_assert(!std::is_default_constructible_v<wiola::media::LocalFileStream>);

class LocalFileStreamTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        std::ofstream output{path, std::ios::binary};
        output << "abcd";
        output.close();
        ASSERT_TRUE(output);
        auto opened{wiola::media::LocalFileStream::open(path)};
        ASSERT_TRUE(opened);
        stream = std::move(*opened);
    }

    wiola::testing::TemporaryDirectory directory;
    const std::filesystem::path path{directory.path() / "bytes.bin"};
    std::unique_ptr<wiola::media::LocalFileStream> stream;
};

TEST_F(LocalFileStreamTest, ReadsSeeksAndReportsItsCursor)
{
    std::array<std::byte, 2> output{};
    const auto read{stream->read(output)};
    ASSERT_TRUE(read);
    EXPECT_EQ(*read, output.size());
    EXPECT_EQ(output, (std::array{std::byte{'a'}, std::byte{'b'}}));
    ASSERT_TRUE(stream->seek(-1, SeekOrigin::current));
    const auto position{stream->tell()};
    ASSERT_TRUE(position);
    EXPECT_EQ(*position, 1U);
    ASSERT_TRUE(stream->seek(-1, SeekOrigin::end));
    const auto tail{stream->read(output)};
    ASSERT_TRUE(tail);
    EXPECT_EQ(*tail, 1U);
    EXPECT_EQ(output[0], std::byte{'d'});
}

TEST_F(LocalFileStreamTest, ShortReadsAndRepeatedEofRemainSuccessful)
{
    std::array<std::byte, 6> output{};
    const auto read{stream->read(output)};
    ASSERT_TRUE(read);
    EXPECT_EQ(*read, 4U);
    const auto eof{stream->read(output)};
    ASSERT_TRUE(eof);
    EXPECT_EQ(*eof, 0U);
    ASSERT_TRUE(stream->seek(0, SeekOrigin::start));
    const auto repeated{stream->read(output)};
    ASSERT_TRUE(repeated);
    EXPECT_EQ(*repeated, 4U);
}

TEST_F(LocalFileStreamTest, FailedSeeksPreserveTheCursorAndTheNextRead)
{
    const std::array<std::pair<SeekOrigin, std::int64_t>, 3> movements{
        {{SeekOrigin::start, -1}, {SeekOrigin::current, -3}, {SeekOrigin::end, -5}}
    };
    for (const auto& [origin, offset] : movements) {
        SCOPED_TRACE(offset);
        ASSERT_TRUE(stream->seek(2, SeekOrigin::start));
        const auto rejected{stream->seek(offset, origin)};
        ASSERT_FALSE(rejected);
        EXPECT_EQ(rejected.error().category, ErrorCategory::seek_failed);

        // Each movement targets byte -1. Read immediately: tell or another seek would clear
        // the old failure flag and conceal a poisoned stream.
        std::array<std::byte, 2> output{};
        const auto read{stream->read(output)};
        ASSERT_TRUE(read);
        EXPECT_EQ(*read, output.size());
        EXPECT_EQ(output, (std::array{std::byte{'c'}, std::byte{'d'}}));
        const auto cursor{stream->tell()};
        ASSERT_TRUE(cursor);
        EXPECT_EQ(*cursor, 4U);
    }
}

TEST_F(LocalFileStreamTest, FailedSeekAtEofPreservesAUsableEof)
{
    std::array<std::byte, 6> output{};
    const auto read{stream->read(output)};
    ASSERT_TRUE(read);
    EXPECT_EQ(*read, 4U);
    const auto rejected{stream->seek(-5, SeekOrigin::end)};
    ASSERT_FALSE(rejected);
    const auto eof{stream->read(output)};
    ASSERT_TRUE(eof);
    EXPECT_EQ(*eof, 0U);
}

TEST_F(LocalFileStreamTest, StopAndDeadlineRejectEveryOperationWithoutMovingTheCursor)
{
    ASSERT_TRUE(stream->seek(2, SeekOrigin::start));
    std::stop_source stopped;
    stopped.request_stop();
    using Context = wiola::media::OperationContext;
    const std::array contexts{
        Context{.stop = stopped.get_token(), .deadline = {}                                             },
        Context{.stop = {},                  .deadline = Context::Clock::now() - std::chrono::seconds{1}}
    };
    for (const auto& context : contexts) {
        const auto expected{context.error().category};
        const auto rejected_open{wiola::media::LocalFileStream::open(path, context)};
        ASSERT_FALSE(rejected_open);
        EXPECT_EQ(rejected_open.error().category, expected);
        std::array output{std::byte{0x55}, std::byte{0x55}};
        const auto read{stream->read(output, context)};
        const auto seek{stream->seek(0, SeekOrigin::start, context)};
        const auto tell{stream->tell(context)};
        ASSERT_FALSE(read);
        ASSERT_FALSE(seek);
        ASSERT_FALSE(tell);
        EXPECT_EQ(read.error().category, expected);
        EXPECT_EQ(seek.error().category, expected);
        EXPECT_EQ(tell.error().category, expected);
        EXPECT_EQ(output, (std::array{std::byte{0x55}, std::byte{0x55}}));
        const auto position{stream->tell()};
        ASSERT_TRUE(position);
        EXPECT_EQ(*position, 2U);
    }
    std::array<std::byte, 3> output{};
    const auto read{stream->read(output)};
    ASSERT_TRUE(read);
    EXPECT_EQ(*read, 2U);
    EXPECT_EQ(output[0], std::byte{'c'});
    EXPECT_EQ(output[1], std::byte{'d'});
}

TEST(LocalFileStream, RejectsMissingAndNonregularResourcesAtOpen)
{
    wiola::testing::TemporaryDirectory directory;
    const auto missing{wiola::media::LocalFileStream::open(directory.path() / "missing.bin")};
    const auto folder{wiola::media::LocalFileStream::open(directory.path())};
    ASSERT_FALSE(missing);
    ASSERT_FALSE(folder);
    EXPECT_EQ(missing.error().category, ErrorCategory::unreadable);
    EXPECT_EQ(folder.error().category, ErrorCategory::unreadable);
}

} // namespace
