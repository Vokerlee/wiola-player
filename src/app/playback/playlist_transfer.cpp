/**
 * @file
 * @brief Transactional transfer of media occurrences into saved playlists.
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

#include "playlist_transfer.hpp"

#include <utility>

#include <library/repository_transaction.hpp>

namespace wiola::app {

PlaylistTransfer::PlaylistTransfer(library::InMemoryRepository& repository,
    const ActivePlaylist& active, Persist persist)
    : repository_{repository}
    , active_{active}
    , persist_{std::move(persist)}
{
}

PlaylistTransferResult PlaylistTransfer::add_to_active(const std::vector<MediaTransfer>& media)
{
    return active_.id() ? add_to(*active_.id(), media) : PlaylistTransferResult{};
}

PlaylistTransferResult PlaylistTransfer::add_to(const library::PlaylistId& playlist,
    const std::vector<MediaTransfer>& media)
{
    if (media.empty() || repository_.playlist(playlist) == nullptr)
        return {};

    library::RepositoryTransaction transaction{repository_, persist_};
    PlaylistTransferResult result;

    for (const MediaTransfer& item : media) {
        if (!item.media.valid())
            continue;

        if (repository_.track(item.media) == nullptr) {
            library::TrackRecord record;
            record.id = item.media;

            if (!item.location.empty())
                record.locations = {item.location};

            if (!repository_.put_track(std::move(record)))
                continue;
        }

        if (!repository_.append_track(playlist, item.media))
            continue;

        ++result.added;
    }

    if (result.added == 0)
        return result;

    result.succeeded = transaction.commit();
    if (!result.succeeded) {
        result.added = 0;
    }

    return result;
}

PlaylistTransferResult PlaylistTransfer::move_to(const library::PlaylistId& source,
    const library::PlaylistId& destination, std::span<const library::PlaylistEntryId> entries)
{
    if (source == destination || entries.empty() || repository_.playlist(source) == nullptr ||
        repository_.playlist(destination) == nullptr)
        return {};

    library::RepositoryTransaction transaction{repository_, persist_};
    PlaylistTransferResult result;

    for (const library::PlaylistEntryId& id : entries) {
        const library::PlaylistRecord* source_record = repository_.playlist(source);
        const auto entry =
            std::ranges::find(source_record->entries, id, &library::PlaylistEntryRecord::id);
        if (entry == source_record->entries.end())
            continue;

        const library::MediaId media = entry->media;
        if (!repository_.append_track(destination, media) ||
            !repository_.remove_entry(source, id)) {
            return {};
        }

        ++result.added;
    }

    if (result.added == 0)
        return result;

    result.succeeded = transaction.commit();
    if (!result.succeeded) {
        result.added = 0;
    }

    return result;
}

} // namespace wiola::app
