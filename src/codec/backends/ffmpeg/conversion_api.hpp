/**
 * @file
 * @brief Injectable Swresample operations used by the private PCM converter.
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

#include <cstdint>

struct SwrContext;

namespace wiola::codec::ffmpeg {

struct PcmConversionApi {
    int (*initialize)(SwrContext*){nullptr};
    int (*out_samples)(SwrContext*, int){nullptr};
    int (*convert)(SwrContext*, std::uint8_t**, int, const std::uint8_t* const *, int){nullptr};
};

} // namespace wiola::codec::ffmpeg
