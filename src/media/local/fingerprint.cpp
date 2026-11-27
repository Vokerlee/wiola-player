/**
 * @file
 * @brief Cancellable native content fingerprint reading.
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

#include "fingerprint.hpp"

#include <array>
#include <charconv>
#include <cstdint>
#include <fstream>
#include <string>

namespace wiola::media {
namespace {

void append_hex(std::string& encoded, std::uint64_t value)
{
    // Sixteen base-16 digits hold every uint64_t; this encoding has no locale.
    std::array<char, 16> digits;
    const auto end = std::to_chars(digits.data(), digits.data() + digits.size(), value, 16).ptr;
    encoded.append(digits.size() - static_cast<std::size_t>(end - digits.data()), '0');
    encoded.append(digits.data(), end);
}

} // namespace

std::optional<std::string> fingerprint_media_cancellable(const std::filesystem::path& file,
    std::stop_token stop)
{
    if (stop.stop_requested())
        return std::nullopt;

    std::ifstream input{file, std::ios::binary};

    if (!input.is_open())
        return std::nullopt;

    constexpr std::uint64_t offset{14695981039346656037ULL};
    constexpr std::uint64_t prime{1099511628211ULL};
    std::uint64_t hash{offset};
    std::uint64_t bytes{0};
    std::array<char, 64 * 1024> buffer;

    while (input) {
        if (stop.stop_requested())
            return {};

        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));

        const std::streamsize count{input.gcount()};

        for (std::streamsize index = 0; index < count; ++index) {
            hash ^= static_cast<unsigned char>(buffer[static_cast<std::size_t>(index)]);
            hash *= prime;
        }

        bytes += static_cast<std::uint64_t>(count);
    }

    if (!input.eof())
        return std::nullopt;

    std::string encoded{"fnv1a64:"};
    append_hex(encoded, hash);
    encoded += ':';
    append_hex(encoded, bytes);

    return encoded;
}

std::optional<std::string> fingerprint_media(const std::filesystem::path& file)
{
    return fingerprint_media_cancellable(file, {});
}

} // namespace wiola::media
