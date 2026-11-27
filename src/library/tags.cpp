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

#include "records.hpp"
#include "tags.hpp"

#include <algorithm>
#include <utility>

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

std::optional<std::string> make_tag_key(std::string_view key)
{
    const auto clean = trimmed(key);

    if (!valid_text(clean, max_tag_key_bytes))
        return std::nullopt;

    return std::string{clean};
}

std::optional<TagValue> make_tag_value(std::string key, std::string value)
{
    auto clean_key = make_tag_key(key);
    const std::string_view clean_value = trimmed(value);

    if (!clean_key || !valid_text(clean_value, max_tag_value_bytes))
        return std::nullopt;

    return TagValue{.key = std::move(*clean_key), .value = std::string{clean_value}};
}

std::string normalized_tag_text(std::string_view text)
{
    const std::string_view clean = trimmed(text);
    std::string normalized{clean};

    std::ranges::transform(normalized, normalized.begin(), [](unsigned char character) {
        return static_cast<char>(character >= 'A' && character <= 'Z' ? character + ('a' - 'A')
                                                                      : character);
    });

    return normalized;
}

bool contains_tag_value(std::span<const TagRecord> tags, const TagValue& candidate,
    const TagId& except)
{
    const auto key = normalized_tag_text(candidate.key);
    const auto value = normalized_tag_text(candidate.value);

    return std::ranges::any_of(tags, [&](const TagRecord& tag) {
        return (!except.valid() || tag.id != except) && normalized_tag_text(tag.key) == key &&
            normalized_tag_text(tag.value) == value;
    });
}

} // namespace wiola::library
