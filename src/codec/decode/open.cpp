/**
 * @file
 * @brief Picks a reader for a file.
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

#include <codec/decode/open.hpp>

#include "flac_reader.hpp"
#include "format.hpp"
#include "mp3_reader.hpp"
#include "wav_reader.hpp"

#include <media/local_file_stream.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <string>

namespace wiola::codec {

namespace {

/// Every format this layer reads. Adding one is a line here and nothing else.
std::span<const Format> formats()
{
    static const std::array list{WavReader::format(), FlacReader::format(), Mp3Reader::format()};

    return list;
}

std::string lowercased_extension(const std::filesystem::path& path)
{
    std::string extension{path.extension().string()};
    std::ranges::transform(extension, extension.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    return extension;
}

} // namespace

Opened open_file(const std::filesystem::path& path)
{
    const media::FileFacts facts{.path = path, .extension = lowercased_extension(path)};
    auto probe{media::LocalFileStream::open(facts.path)};

    if (!probe)
        return media::unexpected{probe.error()};

    auto head{FileHead::read(**probe)};

    if (!head)
        return media::unexpected{head.error()};

    if (head->empty())
        return media::unexpected{
            media::Error{.category = media::ErrorCategory::unreadable,
                         .detail = "file header is empty"}
        };

    bool claimed{false};

    // A file is a format if that format's reader accepts it. Everything known about the file only
    // decides who is asked first, which is why the weakest reason still gets its turn.
    for (const Match reason : reasons) {
        for (const Format& format : formats()) {
            if (match_of(format, *head, facts.extension) != reason)
                continue;

            claimed = claimed || reason == Match::signature;

            if (std::unique_ptr<Decoder> decoder{format.open(path)})
                return decoder;
        }
    }

    // A file carrying a format's signature is that format, so a reader refusing it is damage
    // rather than a format nothing here reads.
    return media::unexpected{
        media::Error{.category = claimed ? media::ErrorCategory::damaged
                                         : media::ErrorCategory::unsupported,
                     .detail = claimed ? "recognized media is damaged" : "no reader supports this media"}
    };
}

} // namespace wiola::codec
