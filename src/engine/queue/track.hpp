/**
 * @file
 * @brief Stable queue identity, immutable location, and mutable track metadata.
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
#include <wiola/playback/queue_entry_id.hpp>

#include <wiola/media/metadata.hpp>
#include <wiola/units.hpp>

#include <compare>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace wiola::engine {

/// Facts learned after queueing, separate from the resource they describe.
using TrackMetadata = media::TrackMetadata;

/// One queue entry. Identity and location are assigned once and cannot be replaced by metadata.
class Track {
public:
    /// Creates an unassigned entry; Playlist assigns its stable identity when queueing it.
    [[nodiscard]] static Track of(std::filesystem::path path);

    [[nodiscard]] constexpr QueueEntryId id() const noexcept { return id_; }

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

    [[nodiscard]] const TrackMetadata& metadata() const noexcept { return metadata_; }

    [[nodiscard]] const std::string& title() const noexcept { return metadata_.title; }

    [[nodiscard]] const std::string& artist() const noexcept { return metadata_.artist; }

    [[nodiscard]] const std::string& album() const noexcept { return metadata_.album; }

    [[nodiscard]] const std::string& genre() const noexcept { return metadata_.genre; }

    [[nodiscard]] const std::string& comment() const noexcept { return metadata_.comment; }

    [[nodiscard]] const std::string& composer() const noexcept { return metadata_.composer; }

    [[nodiscard]] constexpr std::optional<std::uint32_t> year() const noexcept
    {
        return metadata_.year;
    }

    [[nodiscard]] constexpr std::optional<std::uint32_t> track_number() const noexcept
    {
        return metadata_.track_number;
    }

    [[nodiscard]] constexpr std::optional<std::uint32_t> disc_number() const noexcept
    {
        return metadata_.disc_number;
    }

    [[nodiscard]] constexpr std::optional<units::Time> duration() const noexcept
    {
        return metadata_.duration;
    }

    [[nodiscard]] const std::shared_ptr<const media::Artwork>& artwork() const noexcept
    {
        return artwork_;
    }

    [[nodiscard]] constexpr std::uint64_t revision() const noexcept { return revision_; }

private:
    QueueEntryId id_{};
    std::filesystem::path path_;
    TrackMetadata metadata_;
    std::shared_ptr<const media::Artwork> artwork_;
    std::uint64_t revision_{0};

    friend class Playlist;
};

} // namespace wiola::engine
