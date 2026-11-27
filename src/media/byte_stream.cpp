// SPDX-License-Identifier: GPL-3.0-or-later

#include "byte_stream.hpp"

namespace wiola::media {

Result<std::size_t> ByteStream::read(std::span<std::byte> out, const OperationContext& context)
{
    if (context.stop_reason() != StopReason::none)
        return unexpected{context.error()};

    if (out.empty())
        return std::size_t{};

    auto count{read_impl(out, context)};

    if (count && *count > out.size())
        return unexpected{
            Error{.category = ErrorCategory::damaged,
                  .detail = "byte stream reported more bytes than requested"}
        };

    return count;
}

Result<void> ByteStream::seek(std::int64_t offset, SeekOrigin origin,
    const OperationContext& context)
{
    if (context.stop_reason() != StopReason::none)
        return unexpected{context.error()};
    return seek_impl(offset, origin, context);
}

Result<std::uint64_t> ByteStream::tell(const OperationContext& context)
{
    if (context.stop_reason() != StopReason::none)
        return unexpected{context.error()};
    return tell_impl(context);
}

} // namespace wiola::media
