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

#include "registry.hpp"

#include <media/codecs/ffmpeg/decoder.hpp>

#include <media/local/file_stream.hpp>

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

std::vector<Backend> default_backends()
{
    return {ffmpeg::backend()};
}

} // namespace

MediaBackendRegistry::MediaBackendRegistry()
    : MediaBackendRegistry{default_backends()}
{
}

MediaBackendRegistry::MediaBackendRegistry(std::vector<Backend> backends, std::size_t max_attempts)
    : backends_{std::move(backends)}
    , max_attempts_{max_attempts}
{
    if (max_attempts_ == 0)
        throw std::invalid_argument{"media backend attempt limit must be positive"};
}

media::MetadataWriteCapabilities
MediaBackendRegistry::metadata_write_capabilities(const std::filesystem::path& path) const
{
    const std::string extension = lowercased_extension(path);

    for (const Backend& backend : backends_) {
        for (const Format& format : backend.formats) {
            if (std::ranges::contains(format.extensions, extension))
                return format.metadata_writes;
        }
    }

    return {};
}

std::vector<std::string> MediaBackendRegistry::playback_extensions() const
{
    std::vector<std::string> extensions;
    for (const Backend& backend : backends_) {
        if (!backend.capabilities.playback)
            continue;
        for (const Format& format : backend.formats)
            for (const std::string& extension : format.extensions)
                extensions.push_back(extension);
    }
    std::ranges::sort(extensions);
    extensions.erase(std::unique(extensions.begin(), extensions.end()), extensions.end());
    return extensions;
}

media::DecoderOpenResult MediaBackendRegistry::open(const std::filesystem::path& path,
    const media::OperationContext& context) const
{
    const media::FileFacts facts{.path = path, .extension = lowercased_extension(path)};
    auto probe{media::LocalFileStream::open(facts.path, context)};

    if (!probe)
        return media::unexpected{probe.error()};

    auto head{FileHead::read(**probe, context)};

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
        for (const Backend& backend : backends_) {
            if (!backend.capabilities.playback ||
                match_of(backend, *head, facts.extension) != reason)
                continue;

            if (context.stop_reason() != media::StopReason::none)
                return media::unexpected{context.error()};

            if (attempts++ == max_attempts_)
                return media::unexpected{
                    media::Error{.category = media::ErrorCategory::unsupported,
                                 .detail = "media backend probe limit reached"}
                };

            claimed = claimed || reason == Match::signature;

            auto opened{backend.open(path, context)};
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
