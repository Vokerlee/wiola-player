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

#pragma once

#include <codec/decode/open.hpp>
#include <media/byte_stream.hpp>

#include <array>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace wiola::codec {

/// A byte pattern a format writes at a fixed distance from the start of every file it produces.
struct Marker {
    std::size_t offset;
    std::string bytes;
};

/// The first bytes of a file, as many as it had.
class FileHead final {
public:
    /// Reads leading bytes from the current cursor, distinguishing EOF from read failure.
    [[nodiscard]] static media::Result<FileHead> read(media::ByteStream& stream);

    /// Whether `marker` is present where it says it should be.
    [[nodiscard]] bool carries(const Marker& marker) const noexcept;

    /// Whether nothing could be read at all.
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }

private:
    /// Bytes kept, enough for the longest marker any format declares.
    static constexpr std::size_t capacity{16};

    std::array<std::byte, capacity> bytes_{};
    std::size_t size_{0};
};

/**
 * One format: how it is recognized, what it is called, and how to open it.
 *
 * A decoder knows none of this. Recognition is data here rather than code, so a format is added
 * by describing it, and a format that describes itself badly cannot mislead the ones that do not.
 */
struct BackendCapabilities {
    bool playback{true};
    bool metadata{true};
};

struct Format {
    using OpenFunction = std::function<Opened(const std::filesystem::path&)>;

    Format(std::string name, std::span<const Marker> markers,
        std::span<const std::string_view> extensions, OpenFunction open,
        BackendCapabilities capabilities = {})
        : name{std::move(name)}
        , markers{markers.begin(), markers.end()}
        , open{std::move(open)}
        , capabilities{capabilities}
    {
        this->extensions.reserve(extensions.size());
        for (const std::string_view extension : extensions)
            this->extensions.emplace_back(extension);
    }

    std::string name;
    std::vector<Marker> markers;
    std::vector<std::string> extensions;
    OpenFunction open;
    BackendCapabilities capabilities{};
};

struct Backend {
    Backend(Format format)
        : name{format.name}
        , open{format.open}
        , capabilities{format.capabilities}
        , formats{std::move(format)}
    {
    }

    Backend(std::string name, std::span<const Format> formats, Format::OpenFunction open,
        BackendCapabilities capabilities = {})
        : name{std::move(name)}
        , open{std::move(open)}
        , capabilities{capabilities}
        , formats{formats.begin(), formats.end()}
    {
    }

    std::string name;
    Format::OpenFunction open;
    BackendCapabilities capabilities{};
    std::vector<Format> formats;
};

/// Why a format is worth trying, best reason first. Ordering the attempts is the whole of format
/// detection: a file is this format if the format's own reader accepts it, and nothing else.
enum class Match {
    signature,
    extension,
    possible,
    unlikely,
};

/// Every reason there is, strongest first.
inline constexpr std::array reasons{
    Match::signature,
    Match::extension,
    Match::possible,
    Match::unlikely,
};

/// Why `format` is worth trying for a file with this head and this `extension`.
[[nodiscard]] Match match_of(const Format& format, const FileHead& head,
    std::string_view extension);
[[nodiscard]] Match match_of(const Backend& backend, const FileHead& head,
    std::string_view extension);

} // namespace wiola::codec
