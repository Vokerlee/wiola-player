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

#pragma once

#include <wiola/library/identity.hpp>

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace wiola::library {

inline constexpr std::size_t max_tags_per_track{256};
inline constexpr std::size_t max_tag_key_bytes{128};
inline constexpr std::size_t max_tag_value_bytes{1024};

struct TagRecord;

struct TagValue {
    std::string key;
    std::string value;
};

/// Validates a standalone key with the same trimming and byte limits as a complete tag.
[[nodiscard]] std::optional<std::string> make_tag_key(std::string_view key);

/// Trims surrounding ASCII whitespace and preserves the casing and interior text.
[[nodiscard]] std::optional<TagValue> make_tag_value(std::string key, std::string value);

/// Produces the comparison form: trimmed with ASCII case folded and other bytes preserved.
[[nodiscard]] std::string normalized_tag_text(std::string_view text);

/// Tests normalized key/value equivalence, optionally excluding one existing occurrence.
[[nodiscard]] bool contains_tag_value(std::span<const TagRecord> tags, const TagValue& candidate,
    const TagId& except = {});

} // namespace wiola::library
