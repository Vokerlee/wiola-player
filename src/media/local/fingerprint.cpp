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
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace wiola::media {

std::optional<std::string> fingerprint_media_cancellable(const std::filesystem::path& file,
    std::stop_token stop)
{
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

    std::ostringstream encoded;
    encoded << "fnv1a64:" << std::hex << std::setfill('0') << std::setw(16) << hash << ':'
            << std::setw(16) << bytes;

    return encoded.str();
}

std::optional<std::string> fingerprint_media(const std::filesystem::path& file)
{
    return fingerprint_media_cancellable(file, {});
}

} // namespace wiola::media
