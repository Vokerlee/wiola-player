/**
 * @file
 * @brief Checked, nonthrowing C callback adapters for decoder byte streams.
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

#pragma once

#include <media/byte_stream.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

namespace wiola::codec {

template<typename Origin, Origin from_start, Origin from_current, Origin from_end, typename Cursor,
    typename Offset>
struct StreamCallbacks {
    static std::size_t read(void* user, void* out, std::size_t num_bytes) noexcept
    {
        try {
            auto& stream = *static_cast<media::ByteStream*>(user);
            const auto read = stream.read(std::span{static_cast<std::byte*>(out), num_bytes});
            return read ? *read : 0;
        } catch (...) {
            return 0;
        }
    }

    static unsigned int seek(void* user, Offset offset, Origin origin) noexcept
    {
        try {
            if (offset < std::numeric_limits<std::int64_t>::min() ||
                offset > std::numeric_limits<std::int64_t>::max())
                return 0U;

            const auto from = origin == from_start ? std::optional{media::SeekOrigin::start}
                : origin == from_current           ? std::optional{media::SeekOrigin::current}
                : origin == from_end               ? std::optional{media::SeekOrigin::end}
                                                   : std::nullopt;

            if (!from)
                return 0U;

            auto& stream = *static_cast<media::ByteStream*>(user);
            return stream.seek(static_cast<std::int64_t>(offset), *from) ? 1U : 0U;
        } catch (...) {
            return 0U;
        }
    }

    static unsigned int tell(void* user, Cursor* cursor) noexcept
    {
        try {
            auto& stream = *static_cast<media::ByteStream*>(user);
            const auto position = stream.tell();

            if (!position || *position > std::numeric_limits<Cursor>::max())
                return 0U;

            *cursor = static_cast<Cursor>(*position);
            return 1U;
        } catch (...) {
            return 0U;
        }
    }
};

} // namespace wiola::codec
