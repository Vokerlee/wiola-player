/**
 * @file
 * @brief UTF-8 playlist-name normalization and compatible length validation.
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

#include "playlist_name.hpp"
#include <cstdint>
#include <utility>

namespace wiola::app {
namespace {

struct Scalar {
    char32_t value;
    std::size_t bytes;
    bool valid;
};

Scalar next_scalar(std::string_view text)
{
    const auto first = static_cast<std::uint8_t>(text.front());
    if (first < 0x80)
        return {first, 1, true};
    std::size_t width;
    char32_t value, minimum;
    if (first >= 0xc2 && first <= 0xdf) {
        width = 2;
        value = first & 0x1f;
        minimum = 0x80;
    } else if (first >= 0xe0 && first <= 0xef) {
        width = 3;
        value = first & 0x0f;
        minimum = 0x800;
    } else if (first >= 0xf0 && first <= 0xf4) {
        width = 4;
        value = first & 0x07;
        minimum = 0x10000;
    } else {
        return {0xfffd, 1, false};
    }
    if (text.size() < width)
        return {0xfffd, 1, false};
    for (std::size_t i = 1; i != width; ++i) {
        const auto byte = static_cast<std::uint8_t>(text[i]);
        if (byte < 0x80 || byte > 0xbf)
            return {0xfffd, 1, false};
        value = (value << 6) | (byte & 0x3f);
    }
    if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff))
        return {0xfffd, 1, false};
    return {value, width, true};
}

bool whitespace(char32_t value)
{
    // Unicode White_Space, matching the player's existing QString trimming contract.
    return (value >= 0x09 && value <= 0x0d) || value == 0x20 || value == 0x85 || value == 0xa0 ||
        value == 0x1680 || (value >= 0x2000 && value <= 0x200a) || value == 0x2028 ||
        value == 0x2029 || value == 0x202f || value == 0x205f || value == 0x3000;
}

} // namespace

PlaylistName::PlaylistName(std::string value)
    : value_{std::move(value)}
{
}

std::optional<PlaylistName> PlaylistName::parse(std::string_view text)
{
    // QString's UTF-8 conversion consumes one initial BOM and repairs each malformed byte.
    if (text.starts_with("\xef\xbb\xbf"))
        text.remove_prefix(3);
    std::size_t begin = text.size(), end = 0, units = 0;
    for (std::size_t offset = 0; offset != text.size();) {
        const auto scalar = next_scalar(text.substr(offset));
        const bool space = whitespace(scalar.value);
        if (!space && begin == text.size())
            begin = offset;
        if (begin != text.size()) {
            units += scalar.value > 0xffff ? 2 : 1;
            if (!space) {
                if (units > max_utf16_units)
                    return std::nullopt;
                end = offset + scalar.bytes;
            }
        }
        offset += scalar.bytes;
    }
    if (begin == text.size())
        return std::nullopt;
    std::string normalized;
    normalized.reserve(end - begin);
    for (std::size_t offset = begin; offset != end;) {
        const auto scalar = next_scalar(text.substr(offset));
        if (scalar.valid)
            normalized.append(text.substr(offset, scalar.bytes));
        else
            normalized.append("\xef\xbf\xbd");
        offset += scalar.bytes;
    }
    return PlaylistName{std::move(normalized)};
}

} // namespace wiola::app
