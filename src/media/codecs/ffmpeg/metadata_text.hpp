/**
 * @file
 * @brief Private bounded FFmpeg metadata and attached-artwork reader.
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

#include <initializer_list>
#include <string>

struct AVDictionary;

namespace wiola::codec::ffmpeg {

/// Bounded dictionary reads; repairs contradictory UTF-16 only with two numeric witnesses.
class MetadataText final {
public:
    MetadataText(AVDictionary* primary, AVDictionary* secondary, bool legacy_id3);
    [[nodiscard]] std::string field(const char* name) const;
    [[nodiscard]] std::string first(std::initializer_list<const char*> names) const;

private:
    AVDictionary* primary_;
    AVDictionary* secondary_;
    bool primary_swapped_;
    bool secondary_swapped_;
};

} // namespace wiola::codec::ffmpeg
