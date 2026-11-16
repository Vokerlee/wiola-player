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

#include <codec/decode/registry.hpp>

#include <codec/backends/dr_libs/flac_reader.hpp>
#include <codec/backends/dr_libs/mp3_reader.hpp>
#include <codec/backends/dr_libs/wav_reader.hpp>

#include <media/local_file_stream.hpp>

#include <algorithm>
#include <cctype>
#include <optional>
#include <stdexcept>
#include <string>

namespace wiola::codec {

namespace {

std::string lowercased_extension(const std::filesystem::path& path)
{
    std::string extension{path.extension().string()};
    std::ranges::transform(extension, extension.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension;
}

} // namespace

MediaBackendRegistry::MediaBackendRegistry()
    : MediaBackendRegistry{
          {WavReader::format(), FlacReader::format(), Mp3Reader::format()}
}
{
}

MediaBackendRegistry::MediaBackendRegistry(std::vector<Format> backends, std::size_t max_attempts)
    : backends_{std::move(backends)}
    , max_attempts_{max_attempts}
{
    if (max_attempts_ == 0)
        throw std::invalid_argument{"media backend attempt limit must be positive"};
}

std::vector<std::string> MediaBackendRegistry::playback_extensions() const
{
    std::vector<std::string> extensions;
    for (const Format& backend : backends_) {
        if (!backend.capabilities.playback)
            continue;
        for (const std::string_view extension : backend.extensions)
            extensions.emplace_back(extension);
    }
    return extensions;
}

Opened MediaBackendRegistry::open(const std::filesystem::path& path) const
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
    std::size_t attempts{0};
    std::optional<media::Error> failure;

    for (const Match reason : reasons) {
        for (const Format& format : backends_) {
            if (!format.capabilities.playback || match_of(format, *head, facts.extension) != reason)
                continue;

            if (attempts++ == max_attempts_)
                return media::unexpected{
                    media::Error{.category = media::ErrorCategory::unsupported,
                                 .detail = "media backend probe limit reached"}
                };

            claimed = claimed || reason == Match::signature;

            auto opened{format.open(path)};
            if (opened)
                return opened;

            if (opened.error().category == media::ErrorCategory::unreadable)
                return opened;

            if (opened.error().category != media::ErrorCategory::not_mine &&
                (!failure || opened.error().category == media::ErrorCategory::damaged))
                failure = opened.error();
        }
    }

    if (failure)
        return media::unexpected{std::move(*failure)};

    return media::unexpected{
        media::Error{.category = claimed ? media::ErrorCategory::damaged
                                         : media::ErrorCategory::unsupported,
                     .detail = claimed ? "recognized media is damaged" : "no reader supports this media"}
    };
}

} // namespace wiola::codec
