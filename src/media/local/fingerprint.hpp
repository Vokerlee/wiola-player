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

#pragma once

#include <filesystem>
#include <optional>
#include <stop_token>
#include <string>

namespace wiola::media {

/// Streams a local file; the digest is for identity matching, not authentication.
/// Cancellation and unreadable inputs produce no digest, including partial reads.
[[nodiscard]] std::optional<std::string>
fingerprint_media_cancellable(const std::filesystem::path& file, std::stop_token stop);
[[nodiscard]] std::optional<std::string> fingerprint_media(const std::filesystem::path& file);

} // namespace wiola::media
