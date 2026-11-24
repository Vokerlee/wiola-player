// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <wiola/media/byte_stream.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace wiola::testing {

/// Owned forward-only bytes for signature and portable stream contracts.
class MemoryByteStream final : public media::ByteStream {
public:
    explicit MemoryByteStream(std::vector<std::byte> bytes = {})
        : bytes_{std::move(bytes)}
    {
    }

    void fail_reads() noexcept { failing_ = true; }

private:
    media::Result<std::size_t> read_impl(std::span<std::byte> out,
        const media::OperationContext&) override
    {
        if (failing_)
            return media::unexpected{
                media::Error{media::ErrorCategory::read_failed, "controlled byte read failed"}
            };
        const auto count{std::min(out.size(), bytes_.size() - cursor_)};
        std::copy_n(bytes_.begin() + static_cast<std::ptrdiff_t>(cursor_), count, out.begin());
        cursor_ += count;
        return count;
    }

    media::Result<void> seek_impl(std::int64_t, media::SeekOrigin,
        const media::OperationContext&) override
    {
        return media::unexpected{
            media::Error{media::ErrorCategory::unsupported, "memory fixture is forward-only"}
        };
    }

    media::Result<std::uint64_t> tell_impl(const media::OperationContext&) override
    {
        return static_cast<std::uint64_t>(cursor_);
    }

    std::vector<std::byte> bytes_;
    std::size_t cursor_{};
    bool failing_{false};
};

} // namespace wiola::testing
