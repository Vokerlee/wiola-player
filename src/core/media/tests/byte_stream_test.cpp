// SPDX-License-Identifier: GPL-3.0-or-later

#include <fakes/byte_stream.hpp>
#include <wiola/media/byte_stream.hpp>

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stop_token>

namespace {

using namespace wiola::media;

/// Observes the real public admission methods without implementing a second I/O policy.
class ObservedStream final : public ByteStream {
public:
    Result<std::size_t> next_read{std::size_t{}};
    Result<void> next_seek{};
    Result<std::uint64_t> next_tell{std::uint64_t{}};
    std::size_t reads{}, seeks{}, tells{};
    OperationContext observed;
    std::int64_t offset{};
    SeekOrigin origin{};

private:
    Result<std::size_t> read_impl(std::span<std::byte>, const OperationContext& context) override
    {
        ++reads;
        observed = context;
        return next_read;
    }

    Result<void> seek_impl(std::int64_t value, SeekOrigin from,
        const OperationContext& context) override
    {
        ++seeks;
        offset = value;
        origin = from;
        observed = context;
        return next_seek;
    }

    Result<std::uint64_t> tell_impl(const OperationContext& context) override
    {
        ++tells;
        observed = context;
        return next_tell;
    }
};

TEST(ByteStream, EmptyReadSucceedsWithoutInvokingTheBackend)
{
    ObservedStream stream;
    const auto empty{stream.read({})};
    ASSERT_TRUE(empty);
    EXPECT_EQ(*empty, 0U);
    EXPECT_EQ(stream.reads, 0U);
}

TEST(ByteStream, StopAndDeadlineRejectEveryOperationBeforeBackendWork)
{
    std::stop_source stopped;
    stopped.request_stop();
    const std::array contexts{
        OperationContext{.stop = stopped.get_token(), .deadline = {}                         },
        OperationContext{.stop = {},
                         .deadline = OperationContext::Clock::now() - std::chrono::seconds{1}}
    };
    for (const auto& context : contexts) {
        ObservedStream stream;
        std::array output{std::byte{0x55}, std::byte{0x55}};
        const auto read{stream.read(output, context)};
        const auto empty{stream.read({}, context)};
        const auto seek{stream.seek(1, SeekOrigin::current, context)};
        const auto tell{stream.tell(context)};
        ASSERT_FALSE(read);
        ASSERT_FALSE(empty);
        ASSERT_FALSE(seek);
        ASSERT_FALSE(tell);
        EXPECT_EQ(read.error().category, context.error().category);
        EXPECT_EQ(empty.error().category, context.error().category);
        EXPECT_EQ(seek.error().category, context.error().category);
        EXPECT_EQ(tell.error().category, context.error().category);
        EXPECT_EQ(output, (std::array{std::byte{0x55}, std::byte{0x55}}));
        EXPECT_EQ(stream.reads, 0U);
        EXPECT_EQ(stream.seeks, 0U);
        EXPECT_EQ(stream.tells, 0U);
    }
}

TEST(ByteStream, AdmittedOperationsForwardContextAndArguments)
{
    ObservedStream stream;
    std::stop_source active;
    const OperationContext context{.stop = active.get_token(),
        .deadline = OperationContext::Clock::time_point::max()};
    std::array<std::byte, 4> output{};
    stream.next_read = Result<std::size_t>{output.size()};
    const auto read{stream.read(output, context)};
    ASSERT_TRUE(read);
    EXPECT_EQ(*read, output.size());
    EXPECT_EQ(stream.observed.stop, context.stop);
    EXPECT_EQ(stream.observed.deadline, context.deadline);
    EXPECT_TRUE(stream.seek(-2, SeekOrigin::end, context));
    EXPECT_EQ(stream.offset, -2);
    EXPECT_EQ(stream.origin, SeekOrigin::end);
    EXPECT_EQ(stream.observed.stop, context.stop);
    EXPECT_EQ(stream.observed.deadline, context.deadline);
    stream.next_tell = Result<std::uint64_t>{7};
    const auto position{stream.tell(context)};
    ASSERT_TRUE(position);
    EXPECT_EQ(*position, 7U);
    EXPECT_EQ(stream.observed.stop, context.stop);
    EXPECT_EQ(stream.observed.deadline, context.deadline);
    EXPECT_EQ(stream.reads, 1U);
    EXPECT_EQ(stream.seeks, 1U);
    EXPECT_EQ(stream.tells, 1U);
}

TEST(ByteStream, RejectsAnOverreportedReadCountWithoutAcceptingItAsProgress)
{
    ObservedStream stream;
    std::array<std::byte, 4> output{};
    stream.next_read = Result<std::size_t>{output.size() + 1};
    const auto result{stream.read(output)};
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().category, ErrorCategory::damaged);
    EXPECT_EQ(result.error().detail, "byte stream reported more bytes than requested");
    EXPECT_EQ(stream.reads, 1U);
}

TEST(ByteStream, TypedBackendFailuresRemainDistinctFromZeroPositionOrEof)
{
    ObservedStream stream;
    stream.next_read = unexpected{
        Error{ErrorCategory::read_failed, "read failed"}
    };
    stream.next_seek = unexpected{
        Error{ErrorCategory::unsupported, "seek unsupported"}
    };
    stream.next_tell = unexpected{
        Error{ErrorCategory::seek_failed, "cursor unavailable"}
    };
    std::array<std::byte, 1> output{};
    const auto read{stream.read(output)};
    const auto seek{stream.seek(0, SeekOrigin::start)};
    const auto tell{stream.tell()};
    ASSERT_FALSE(read);
    ASSERT_FALSE(seek);
    ASSERT_FALSE(tell);
    EXPECT_EQ(read.error().category, ErrorCategory::read_failed);
    EXPECT_EQ(read.error().detail, "read failed");
    EXPECT_EQ(seek.error().category, ErrorCategory::unsupported);
    EXPECT_EQ(seek.error().detail, "seek unsupported");
    EXPECT_EQ(tell.error().category, ErrorCategory::seek_failed);
    EXPECT_EQ(tell.error().detail, "cursor unavailable");
}

TEST(ByteStream, ShortReadAndEofKeepTheirByteCountAndOwnedSourceLifetime)
{
    wiola::testing::MemoryByteStream stream{
        {std::byte{1}, std::byte{2}}
    };
    std::array<std::byte, 4> output{};
    const auto read{stream.read(output)};
    ASSERT_TRUE(read);
    EXPECT_EQ(*read, 2U);
    EXPECT_EQ(output[0], std::byte{1});
    EXPECT_EQ(output[1], std::byte{2});
    const auto eof{stream.read(output)};
    ASSERT_TRUE(eof);
    EXPECT_EQ(*eof, 0U);
    const auto cursor{stream.tell()};
    ASSERT_TRUE(cursor);
    EXPECT_EQ(*cursor, 2U);
    const auto seek{stream.seek(0, SeekOrigin::start)};
    ASSERT_FALSE(seek);
    EXPECT_EQ(seek.error().category, ErrorCategory::unsupported);
    stream.fail_reads();
    const auto failed{stream.read(output)};
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().category, ErrorCategory::read_failed);
}

} // namespace
