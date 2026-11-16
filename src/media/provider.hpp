/**
 * @file
 * @brief Format-neutral access to an identified local media resource.
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

#include <codec/decode/decoder.hpp>
#include <core/macros.hpp>
#include <media/media.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace wiola::media {

class MediaAsset {
public:
    NO_COPY_SEMANTIC(MediaAsset);
    NO_MOVE_SEMANTIC(MediaAsset);

    virtual ~MediaAsset() = default;

    virtual Result<MediaInfo> inspect() = 0;
    virtual Result<Metadata> read_metadata() = 0;
    virtual Result<std::unique_ptr<codec::Decoder>> create_decoder() = 0;

protected:
    MediaAsset() = default;
};

class MediaProvider {
public:
    NO_COPY_SEMANTIC(MediaProvider);
    NO_MOVE_SEMANTIC(MediaProvider);

    virtual ~MediaProvider() = default;

    /// Identifies a resource without decoding it or creating playback/output state.
    virtual Result<std::unique_ptr<MediaAsset>> open(const std::filesystem::path& path) = 0;

    [[nodiscard]] virtual std::vector<std::string> playback_extensions() const = 0;

protected:
    MediaProvider() = default;
};

} // namespace wiola::media
