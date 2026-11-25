/**
 * @file
 * @brief Normalized portable saved-playlist names.
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

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace wiola::app {

/// Normalized UTF-8 playlist name, preserving the existing UTF-16 length budget.
class PlaylistName final {
public:
    static constexpr std::size_t max_utf16_units = 256;

    [[nodiscard]] static std::optional<PlaylistName> parse(std::string_view text);

    [[nodiscard]] const std::string& value() const noexcept { return value_; }

private:
    explicit PlaylistName(std::string value);

    std::string value_;
};

} // namespace wiola::app
