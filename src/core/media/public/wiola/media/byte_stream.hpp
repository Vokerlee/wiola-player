/**
 * @file
 * @brief Narrow seekable byte access independent of filenames and backend libraries.
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

#pragma once

#include <wiola/media/operation_context.hpp>
#include <wiola/media/result.hpp>

#include <cstddef>
#include <cstdint>
#include <span>

namespace wiola::media {

enum class SeekOrigin {
    start,
    current,
    end,
};

class ByteStream {
public:
    ByteStream(const ByteStream&) = delete;
    ByteStream& operator=(const ByteStream&) = delete;
    ByteStream(ByteStream&&) = delete;
    ByteStream& operator=(ByteStream&&) = delete;

    virtual ~ByteStream() = default;

    /// A short successful read is EOF; an error is never encoded as a short read.
    [[nodiscard]] virtual Result<std::size_t> read(std::span<std::byte> out,
        const OperationContext& context = {}) = 0;
    [[nodiscard]] virtual Result<void> seek(std::int64_t offset, SeekOrigin origin,
        const OperationContext& context = {}) = 0;
    [[nodiscard]] virtual Result<std::uint64_t> tell(const OperationContext& context = {}) = 0;

protected:
    ByteStream() = default;
};

} // namespace wiola::media
