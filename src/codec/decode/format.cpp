/**
 * @file
 * @brief What this layer knows about a format, apart from how to decode it.
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

#include "format.hpp"

#include <algorithm>

namespace wiola::codec {

media::Result<FileHead> FileHead::read(media::ByteStream& stream,
    const media::OperationContext& context)
{
    FileHead head;
    const auto read = stream.read(head.bytes_, context);

    if (!read)
        return media::unexpected{read.error()};

    head.size_ = *read;
    return head;
}

bool FileHead::carries(const Marker& marker) const noexcept
{
    if (marker.offset > size_ || marker.bytes.size() > size_ - marker.offset)
        return false;

    return std::ranges::equal(marker.bytes,
        std::span{bytes_}.subspan(marker.offset, marker.bytes.size()),
        [](char expected, std::byte actual) {
            return static_cast<unsigned char>(expected) == std::to_integer<unsigned char>(actual);
        });
}

Match match_of(const Format& format, const FileHead& head, std::string_view extension)
{
    const auto carried = [&head](const Marker& marker) {
        return head.carries(marker);
    };

    if (!format.markers.empty() && std::ranges::all_of(format.markers, carried))
        return Match::signature;

    if (std::ranges::contains(format.extensions, extension))
        return Match::extension;

    // Nothing was found, but a format with no signature was never going to be found by looking.
    if (format.markers.empty())
        return Match::possible;

    return Match::unlikely;
}

Match match_of(const Backend& backend, const FileHead& head, std::string_view extension)
{
    Match best{Match::unlikely};
    for (const Format& format : backend.formats) {
        const Match match{match_of(format, head, extension)};
        if (match == Match::signature)
            return match;
        if (static_cast<int>(match) < static_cast<int>(best))
            best = match;
    }
    return best;
}

} // namespace wiola::codec
