/**
 * @file
 * @brief Content fingerprints and relocation reconciliation for media records.
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
#include <stop_token>

#include <library/records.hpp>

#include <optional>
#include <span>
#include <string>

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

/// Streams the file; the fingerprint is for identity matching, not cryptographic authentication.
[[nodiscard]] std::optional<std::string>
fingerprint_media_cancellable(const std::filesystem::path& file, std::stop_token stop);
[[nodiscard]] std::optional<std::string> fingerprint_media(const std::filesystem::path& file);

/// Returns an identity only when exactly one stored fingerprint matches the candidate.
[[nodiscard]] std::optional<MediaId> match_relocation(std::span<const TrackRecord> tracks,
    const std::filesystem::path& candidate);

/// Reconciles a known track with a readable location while preserving all user-owned metadata.
[[nodiscard]] std::optional<Relocation> reconcile_location(TrackRecord& track,
    const std::filesystem::path& location);

/// Applies facts already read by a worker; this overload performs no file I/O.
[[nodiscard]] std::optional<Relocation> reconcile_location(TrackRecord& track,
    const std::filesystem::path& location, std::string fingerprint);

} // namespace wiola::library
