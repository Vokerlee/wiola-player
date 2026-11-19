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

    const auto previous_tracks = repository_.tracks();
    const auto previous_playlists = repository_.playlists();
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

    result.succeeded = persist_(repository_);
    if (!result.succeeded) {
        static_cast<void>(repository_.replace_all(previous_tracks, previous_playlists));
        result.added = 0;
    }

    return result;
}

} // namespace wiola::app
