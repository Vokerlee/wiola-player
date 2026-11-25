/**
 * @file
 * @brief Owned immutable observation of one playback queue occurrence.
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

#include <wiola/media/metadata.hpp>
#include <wiola/media/resource.hpp>
#include <wiola/playback/queue_entry_id.hpp>

#include <cstdint>
#include <memory>
#include <utility>

namespace wiola::engine {

/// Owned facts for one occurrence. A queue exposes const entries; metadata updates preserve
/// identity/resource. Copies retain their own metadata and share only immutable artwork.
class QueueEntry {
public:
    QueueEntry(QueueEntryId id, media::ResourceReference resource,
        media::TrackMetadata metadata = {}, std::shared_ptr<const media::Artwork> artwork = {},
        std::uint64_t revision = 0) noexcept
        : id_{id}
        , resource_{std::move(resource)}
        , metadata_{std::move(metadata)}
        , artwork_{std::move(artwork)}
        , revision_{revision}
    {
    }

    [[nodiscard]] constexpr QueueEntryId id() const noexcept { return id_; }

    [[nodiscard]] const media::ResourceReference& resource() const noexcept { return resource_; }

    [[nodiscard]] const media::TrackMetadata& metadata() const noexcept { return metadata_; }

    [[nodiscard]] const std::shared_ptr<const media::Artwork>& artwork() const noexcept
    {
        return artwork_;
    }

    [[nodiscard]] constexpr std::uint64_t revision() const noexcept { return revision_; }

private:
    QueueEntryId id_;
    media::ResourceReference resource_;
    media::TrackMetadata metadata_;
    std::shared_ptr<const media::Artwork> artwork_;
    std::uint64_t revision_;

    friend class Playlist;
};

} // namespace wiola::engine
