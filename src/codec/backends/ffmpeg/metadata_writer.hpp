/**
 * @file
 * @brief Transactional FFmpeg metadata remuxing without decoding or source replacement.
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

#include <wiola/media/metadata.hpp>
#include <wiola/media/operation_context.hpp>

#include <filesystem>
#include <functional>

namespace wiola::codec::ffmpeg {

struct MetadataRemuxHooks {
    std::function<int()> after_packet;
};

/// Copies compressed streams to `temporary` while applying only the requested metadata fields.
/// The source is never replaced, and an existing destination is never overwritten.
[[nodiscard]] media::MetadataWriteOutcome remux_metadata(const std::filesystem::path& source,
    const std::filesystem::path& temporary, const media::MetadataWriteRequest& request,
    const media::OperationContext& context = {}, MetadataRemuxHooks hooks = {});

} // namespace wiola::codec::ffmpeg
