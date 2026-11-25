/**
 * @file
 * @brief Explicit saved-document commands for the real playback queue.
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

#include "playback_queue.hpp"

#include <library/location.hpp>
#include <media/local/resource.hpp>

namespace wiola::app {

PlaybackQueueResult PlaybackQueue::replace_with_paths(PlaybackService& session,
    std::vector<std::filesystem::path> tracks) const
{
    if (tracks.empty())
        return {};

    PlaybackQueueResult result;
    result.queued = tracks.size();
    static_cast<void>(session.open(wiola::media::local_resources(tracks)));
    result.open_result = session.open_result();
    result.requested = true;

    return result;
}

PlaybackQueueResult PlaybackQueue::append_paths(PlaybackService& session,
    std::vector<std::filesystem::path> tracks) const
{
    if (tracks.empty())
        return {};

    PlaybackQueueResult result;
    result.queued = tracks.size();
    result.open_result = session.open_result();
    session.add(wiola::media::local_resources(tracks));
    result.requested = true;

    return result;
}

PlaybackQueueResult PlaybackQueue::replace_with_saved_playlist(PlaybackService& session,
    const library::LibraryRepository& repository, const library::PlaylistId& id,
    std::optional<library::PlaylistEntryId> selected) const
{
    const library::PlaylistRecord* playlist = repository.playlist(id);
    if (playlist == nullptr)
        return {};

    PlaybackQueueResult result;
    std::vector<std::filesystem::path> tracks;

    std::optional<std::size_t> selected_position;
    for (const library::PlaylistEntryRecord& entry : playlist->entries) {
        const library::TrackRecord* media = repository.track(entry.media);
        if (media == nullptr || media->locations.empty()) {
            ++result.unavailable;
            continue;
        }

        if (selected == entry.id)
            selected_position = tracks.size();
        tracks.push_back(library::preferred_location(*media));
    }

    if (tracks.empty() || (selected && !selected_position))
        return result;

    PlaybackQueueResult playback = replace_with_paths(session, std::move(tracks));
    playback.unavailable = result.unavailable;
    if (selected_position) {
        static_cast<void>(session.play_track(*selected_position));
        playback.open_result = session.open_result();
    }

    return playback;
}

} // namespace wiola::app
