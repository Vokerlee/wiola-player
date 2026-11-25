/**
 * @file
 * @brief Expected-state checks before replacing metadata or file data.
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

#include <wiola/library/repository.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace wiola::app {

enum class MetadataConflictKind {
    none,
    missing_media,
    metadata_changed,
    content_changed,
    unreadable,
};

struct ExpectedMediaState {
    std::uint64_t metadata_revision{0};
    std::uint64_t content_revision{0};
    std::string fingerprint;
    bool operator==(const ExpectedMediaState&) const = default;
};

class MetadataConflictDetector final {
public:
    using ReadFingerprint = std::function<std::optional<std::string>(const std::filesystem::path&)>;

    explicit MetadataConflictDetector(ReadFingerprint read = {});

    [[nodiscard]] MetadataConflictKind check(const library::LibraryRepository& repository,
        const library::MediaId& media, const ExpectedMediaState& expected,
        const std::filesystem::path& location = {}) const;

private:
    ReadFingerprint read_;
};

} // namespace wiola::app
