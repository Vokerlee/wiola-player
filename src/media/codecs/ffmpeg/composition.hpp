/**
 * @file
 * @brief Private neutral audio decoder backed by FFmpeg libraries.
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

#include <media/codecs/registry.hpp>
#include <wiola/media/provider.hpp>

#include <memory>

namespace wiola::codec::ffmpeg {

/// Explicit construction of the FFmpeg-backed local media composition.
[[nodiscard]] MediaBackendRegistry make_registry();
[[nodiscard]] std::shared_ptr<media::MediaProvider> make_provider();

} // namespace wiola::codec::ffmpeg
