/**
 * @file
 * @brief Custom tag values, normalized matching, and defensive input limits.
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

#include "tags.hpp"

#include <algorithm>
#include <cctype>

namespace wiola::library {

namespace {

bool ascii_space(unsigned char character) noexcept
{
    return character == ' ' || character == '\t' || character == '\n' || character == '\r' ||
        character == '\f' || character == '\v';
}

std::string_view trimmed(std::string_view text) noexcept
{
    const auto first = std::ranges::find_if_not(text,
        [](unsigned char character) { return ascii_space(character); });
    const auto last = std::find_if_not(text.rbegin(),
        std::string_view::const_reverse_iterator{first}, [](unsigned char character) {
            return ascii_space(character);
        }).base();

    return {first, last};
}

bool valid_text(std::string_view text, std::size_t max_bytes) noexcept
{
    return !text.empty() && text.size() <= max_bytes &&
        std::ranges::none_of(text,
            [](unsigned char character) { return character < 0x20 || character == 0x7f; });
}

} // namespace

std::optional<TagValue> make_tag_value(std::string key, std::string value)
{
    const std::string_view clean_key = trimmed(key);
    const std::string_view clean_value = trimmed(value);

    if (!valid_text(clean_key, max_tag_key_bytes) || !valid_text(clean_value, max_tag_value_bytes))
        return std::nullopt;

    return TagValue{.key = std::string{clean_key}, .value = std::string{clean_value}};
}

std::string normalized_tag_text(std::string_view text)
{
    const std::string_view clean = trimmed(text);
    std::string normalized{clean};

    std::ranges::transform(normalized, normalized.begin(),
        [](unsigned char character) { return static_cast<char>(std::tolower(character)); });

    return normalized;
}

} // namespace wiola::library
