/**
 * @file
 * @brief Blocking byte access independent of filenames and backend libraries.
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

    /// A short successful nonempty read is EOF; errors remain typed failures. An empty request
    /// succeeds without backend work after context admission. Reported bytes never exceed out.
    [[nodiscard]] Result<std::size_t> read(std::span<std::byte> out,
        const OperationContext& context = {});
    /// A failed seek preserves the cursor; unsupported movement is a typed backend failure.
    [[nodiscard]] Result<void> seek(std::int64_t offset, SeekOrigin origin,
        const OperationContext& context = {});
    /// Unknown or unsupported position is a failure, distinct from a successful zero cursor.
    [[nodiscard]] Result<std::uint64_t> tell(const OperationContext& context = {});

protected:
    ByteStream() = default;

    /// Hooks execute only after cancellation/deadline admission. A blocking backend continues
    /// to cooperate with the same context; unexpected exceptions retain the caller's policy.
    virtual Result<std::size_t> read_impl(std::span<std::byte> out,
        const OperationContext& context) = 0;
    virtual Result<void> seek_impl(std::int64_t offset, SeekOrigin origin,
        const OperationContext& context) = 0;
    virtual Result<std::uint64_t> tell_impl(const OperationContext& context) = 0;
};

} // namespace wiola::media
