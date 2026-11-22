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

#include "fingerprint.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <utility>

namespace wiola::library {

std::optional<std::string> fingerprint_media_cancellable(const std::filesystem::path& file,
    std::stop_token stop)
{
    std::ifstream input{file, std::ios::binary};

    if (!input.is_open())
        return std::nullopt;

    constexpr std::uint64_t offset{14695981039346656037ULL};
    constexpr std::uint64_t prime{1099511628211ULL};
    std::uint64_t hash{offset};
    std::uint64_t bytes{0};
    std::array<char, 64 * 1024> buffer;

    while (input) {
        if (stop.stop_requested())
            return {};
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize count{input.gcount()};

        for (std::streamsize index = 0; index < count; ++index) {
            hash ^= static_cast<unsigned char>(buffer[static_cast<std::size_t>(index)]);
            hash *= prime;
        }

        bytes += static_cast<std::uint64_t>(count);
    }

    if (!input.eof())
        return std::nullopt;

    std::ostringstream encoded;
    encoded << "fnv1a64:" << std::hex << std::setfill('0') << std::setw(16) << hash << ':'
            << std::setw(16) << bytes;

    return encoded.str();
}

std::optional<std::string> fingerprint_media(const std::filesystem::path& file)
{
    return fingerprint_media_cancellable(file, {});
}

std::optional<MediaId> match_relocation(std::span<const TrackRecord> tracks,
    const std::filesystem::path& candidate)
{
    const auto fingerprint{fingerprint_media(candidate)};

    if (!fingerprint)
        return std::nullopt;

    const TrackRecord* match{nullptr};

    for (const TrackRecord& track : tracks) {
        if (track.fingerprint != *fingerprint)
            continue;

        if (match != nullptr)
            return std::nullopt;

        match = &track;
    }

    return match ? std::optional{match->id} : std::nullopt;
}

std::optional<Relocation> reconcile_location(TrackRecord& track,
    const std::filesystem::path& location)
{
    if (!track.id.valid())
        return std::nullopt;

    auto fingerprint{fingerprint_media(location)};

    if (!fingerprint)
        return std::nullopt;

    return reconcile_location(track, location, std::move(*fingerprint));
}

std::optional<Relocation> reconcile_location(TrackRecord& track,
    const std::filesystem::path& location, std::string fingerprint)
{
    if (!track.id.valid() || fingerprint.empty())
        return std::nullopt;
    RelocationKind kind{RelocationKind::unchanged};

    if (track.fingerprint.empty()) {
        kind = RelocationKind::learned;
    } else if (track.fingerprint != fingerprint) {
        kind = RelocationKind::edited;
        ++track.content_revision;
    }

    track.fingerprint = std::move(fingerprint);
    track.missing = false;

    if (std::ranges::find(track.locations, location) == track.locations.end())
        track.locations.push_back(location);

    return Relocation{.media = track.id, .location = location, .kind = kind};
}

} // namespace wiola::library
