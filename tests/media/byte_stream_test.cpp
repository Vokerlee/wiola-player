/**
 * @file
 * @brief Contract tests for seekable byte streams and decoder callbacks.
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

#include <codec/backends/dr_libs/stream_callbacks.hpp>

#include <media/byte_stream.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

using wiola::media::ByteStream;
using wiola::media::Error;
using wiola::media::ErrorCategory;
using wiola::media::Result;
using wiola::media::SeekOrigin;
using wiola::media::unexpected;

class MemoryStream final : public ByteStream {
public:
    explicit MemoryStream(std::vector<std::byte> bytes)
        : bytes_{std::move(bytes)}
    {
    }

    Result<std::size_t> read(std::span<std::byte> out) override
    {
        if (fail_reads_)
            return unexpected{
                Error{.category = ErrorCategory::read_failed, .detail = "injected"}
            };

        const std::size_t count{std::min(out.size(), bytes_.size() - cursor_)};
        std::copy_n(bytes_.begin() + static_cast<std::ptrdiff_t>(cursor_), count, out.begin());
        cursor_ += count;
        return count;
    }

    Result<void> seek(std::int64_t offset, SeekOrigin origin) override
    {
        const std::int64_t base{origin == SeekOrigin::start ? 0
                : origin == SeekOrigin::current ? static_cast<std::int64_t>(cursor_)
                                                : static_cast<std::int64_t>(bytes_.size())};
        const std::int64_t target{base + offset};

        if (target < 0 || static_cast<std::uint64_t>(target) > bytes_.size())
            return unexpected{
                Error{.category = ErrorCategory::seek_failed, .detail = "out of range"}
            };

        cursor_ = static_cast<std::size_t>(target);
        return {};
    }

    Result<std::uint64_t> tell() override { return cursor_; }

    void fail_reads() { fail_reads_ = true; }

private:
    std::vector<std::byte> bytes_;
    std::size_t cursor_{0};
    bool fail_reads_{false};
};

enum Origin {
    from_start,
    from_current,
    from_end,
};

using Callbacks = wiola::codec::StreamCallbacks<Origin, from_start, from_current, from_end,
    std::uint64_t, std::int64_t>;
using NarrowCallbacks = wiola::codec::StreamCallbacks<Origin, from_start, from_current, from_end,
    std::uint8_t, std::int64_t>;

TEST(ByteStream, ReadsSeeksAndReportsItsCursor)
{
    MemoryStream stream{
        {std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}}
    };
    std::array<std::byte, 2> out{};

    auto read = stream.read(out);
    ASSERT_TRUE(read);
    EXPECT_EQ(*read, 2U);
    EXPECT_EQ(out[0], std::byte{1});
    EXPECT_EQ(out[1], std::byte{2});

    ASSERT_TRUE(stream.seek(-1, SeekOrigin::current));
    auto position = stream.tell();
    ASSERT_TRUE(position);
    EXPECT_EQ(*position, 1U);

    ASSERT_TRUE(stream.seek(-1, SeekOrigin::end));
    read = stream.read(out);
    ASSERT_TRUE(read);
    EXPECT_EQ(*read, 1U);
    EXPECT_EQ(out[0], std::byte{4});
}

TEST(ByteStream, DistinguishesEndOfFileFromReadFailure)
{
    MemoryStream stream{{std::byte{1}}};
    std::array<std::byte, 2> out{};

    auto read = stream.read(out);
    ASSERT_TRUE(read);
    EXPECT_EQ(*read, 1U);

    read = stream.read(out);
    ASSERT_TRUE(read);
    EXPECT_EQ(*read, 0U);

    stream.fail_reads();
    read = stream.read(out);
    ASSERT_FALSE(read);
    EXPECT_EQ(read.error().category, ErrorCategory::read_failed);
}

TEST(StreamCallbacks, RejectsUnknownOriginsAndOutOfRangeSeeks)
{
    MemoryStream stream{
        {std::byte{1}, std::byte{2}}
    };

    EXPECT_EQ(Callbacks::seek(&stream, 0, static_cast<Origin>(99)), 0U);
    EXPECT_EQ(Callbacks::seek(&stream, -1, from_start), 0U);
    EXPECT_EQ(Callbacks::seek(&stream, -1, from_end), 1U);

    std::uint64_t cursor{0};
    EXPECT_EQ(Callbacks::tell(&stream, &cursor), 1U);
    EXPECT_EQ(cursor, 1U);
}

TEST(StreamCallbacks, DoesNotNarrowAnOversizedCursor)
{
    MemoryStream stream(std::vector<std::byte>(256));
    ASSERT_EQ(NarrowCallbacks::seek(&stream, 256, from_start), 1U);

    std::uint8_t cursor{17};
    EXPECT_EQ(NarrowCallbacks::tell(&stream, &cursor), 0U);
    EXPECT_EQ(cursor, 17U);
}

} // namespace
