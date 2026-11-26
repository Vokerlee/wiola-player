/**
 * @file
 * @brief Construction-time registry of ordered media playback backends.
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

#include <media/codecs/format.hpp>
#include <wiola/media/audio_decoder.hpp>

#include <cstddef>
#include <stop_token>
#include <string>
#include <vector>

namespace wiola::codec {

class MediaBackendRegistry {
public:
    explicit MediaBackendRegistry(std::vector<Backend> backends, std::size_t max_attempts = 16);

    /// Uses signatures/extensions only to order bounded, cancellable backend-acceptance probes.
    media::DecoderOpenResult open(const std::filesystem::path& path,
        const media::OperationContext& context = {}) const;

    [[nodiscard]] const std::vector<Backend>& backends() const noexcept { return backends_; }

    [[nodiscard]] media::MetadataWriteCapabilities
    metadata_write_capabilities(const std::filesystem::path& path) const;

    [[nodiscard]] std::vector<std::string> playback_extensions() const;

private:
    std::vector<Backend> backends_;
    std::size_t max_attempts_;
};

} // namespace wiola::codec
