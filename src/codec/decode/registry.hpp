/**
 * @file
 * @brief Construction-time registry of ordered media playback backends.
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

#include <codec/decode/format.hpp>
#include <codec/decode/open.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace wiola::codec {

class MediaBackendRegistry {
public:
    MediaBackendRegistry();
    explicit MediaBackendRegistry(std::vector<Format> backends, std::size_t max_attempts = 16);

    /// Uses signatures/extensions only to order bounded reader-acceptance probes.
    Opened open(const std::filesystem::path& path) const;

    [[nodiscard]] const std::vector<Format>& backends() const noexcept { return backends_; }

    [[nodiscard]] std::vector<std::string> playback_extensions() const;

private:
    std::vector<Format> backends_;
    std::size_t max_attempts_;
};

} // namespace wiola::codec
