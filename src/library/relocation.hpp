/**
 * @file
 * @brief Catalog relocation policy over observed content facts.
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

#include <library/records.hpp>

#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace wiola::library {

enum class RelocationKind {
    unchanged,
    learned,
    edited,
};

struct Relocation {
    MediaId media;
    std::filesystem::path location;
    RelocationKind kind{RelocationKind::unchanged};
};

/// Returns an identity only when exactly one stored record matches a nonempty observed digest.
[[nodiscard]] std::optional<MediaId> match_relocation(std::span<const TrackRecord> tracks,
    std::string_view fingerprint);

/// Applies observed facts without opening a file; user metadata and media identity are preserved.
[[nodiscard]] std::optional<Relocation> reconcile_location(TrackRecord& track,
    const std::filesystem::path& location, std::string fingerprint);

} // namespace wiola::library
