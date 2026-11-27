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

#include "metadata_text.hpp"

extern "C" {
#include <libavutil/avstring.h>
#include <libavutil/dict.h>
}

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace wiola::codec::ffmpeg {
namespace {

constexpr std::size_t max_text_bytes{64 * 1024};

std::string read(AVDictionary* dictionary, const char* name)
{
    const auto* entry = av_dict_get(dictionary, name, nullptr, 0);

    if (!entry || !entry->value)
        return {};

    const auto size = std::strlen(entry->value);

    return size <= max_text_bytes ? std::string{entry->value, size} : std::string{};
}

std::string swap_legacy_text(std::string_view text)
{
    std::string corrected;
    const auto* cursor = reinterpret_cast<const std::uint8_t*>(text.data());
    const auto* end = cursor + text.size();

    while (cursor != end) {
        std::int32_t code{};

        if (av_utf8_decode(&code, &cursor, end, 0) < 0 || code <= 0x7f || code > 0xffff)
            return {};

        code = ((code & 0xff) << 8) | (code >> 8);

        // A conflicting byte-order mark can affect ASCII/Latin/Cyrillic text. Other scripts,
        // surrogate pairs and controls are ambiguous and are retained exactly as FFmpeg read them.
        if (!((code >= 0x20 && code <= 0x7e) || (code >= 0xa0 && code <= 0x024f) ||
                (code >= 0x0370 && code <= 0x052f)))
            return {};

        if (code < 0x80)
            corrected.push_back(static_cast<char>(code));
        else {
            corrected.push_back(static_cast<char>(0xc0 | (code >> 6)));
            corrected.push_back(static_cast<char>(0x80 | (code & 0x3f)));
        }
    }

    return corrected;
}

bool swapped_number(AVDictionary* dictionary, const char* name)
{
    const auto swapped = swap_legacy_text(read(dictionary, name));
    const auto slash = swapped.find('/');
    const auto digits = [](std::string_view value) {
        return !value.empty() && std::ranges::all_of(value, [](char character) {
            return character >= '0' && character <= '9';
        });
    };

    return slash == std::string::npos ? digits(swapped)
                                      : digits(std::string_view{swapped}.substr(0, slash)) &&
            digits(std::string_view{swapped}.substr(slash + 1));
}

bool contradictory_byte_order(AVDictionary* dictionary)
{
    // Do not infer an encoding from a song's language, filename or isolated unusual glyphs.
    return swapped_number(dictionary, "track") && swapped_number(dictionary, "disc");
}

} // namespace

MetadataText::MetadataText(AVDictionary* primary, AVDictionary* secondary, bool legacy_id3)
    : primary_{primary}
    , secondary_{secondary}
    , primary_swapped_{legacy_id3 && contradictory_byte_order(primary)}
    , secondary_swapped_{legacy_id3 && contradictory_byte_order(secondary)}
{
}

std::string MetadataText::field(const char* name) const
{
    auto value = read(primary_, name);
    const bool primary = !value.empty();

    if (!primary)
        value = read(secondary_, name);

    if (primary ? primary_swapped_ : secondary_swapped_) {
        auto corrected = swap_legacy_text(value);
        if (!corrected.empty())
            return corrected;
    }

    return value;
}

std::string MetadataText::first(std::initializer_list<const char*> names) const
{
    for (const auto* name : names)
        if (auto value = field(name); !value.empty())
            return value;
    return {};
}

} // namespace wiola::codec::ffmpeg
