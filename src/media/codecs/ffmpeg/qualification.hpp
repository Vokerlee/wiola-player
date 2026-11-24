/**
 * @file
 * @brief Private machine-readable FFmpeg container/codec/profile qualification set.
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

#include <span>
#include <string_view>

namespace wiola::codec::ffmpeg {

struct Qualification {
    std::string_view format;
    std::string_view container;
    std::string_view codec;
    std::string_view profile;
};

[[nodiscard]] std::span<const Qualification> qualifications() noexcept;
[[nodiscard]] bool is_qualified(std::string_view container, std::string_view codec,
    std::string_view profile) noexcept;

} // namespace wiola::codec::ffmpeg
