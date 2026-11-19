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

namespace wiola::app {

PlaybackQueueResult PlaybackQueue::replace_with_paths(engine::Session& session,
    std::vector<std::filesystem::path> tracks) const
{
    if (tracks.empty())
        return {};

    PlaybackQueueResult result;
    result.queued = tracks.size();
    result.open_result = session.open(std::move(tracks));
    result.requested = true;

    return result;
}

PlaybackQueueResult PlaybackQueue::append_paths(engine::Session& session,
    std::vector<std::filesystem::path> tracks) const
{
    if (tracks.empty())
        return {};

    PlaybackQueueResult result;
    result.queued = tracks.size();
    result.open_result = session.open_result();
    session.add(std::move(tracks));
    result.requested = true;

    return result;
}

PlaybackQueueResult PlaybackQueue::replace_with_saved_playlist(engine::Session& session,
    const library::LibraryRepository& repository, const library::PlaylistId& id) const
{
    const library::PlaylistRecord* playlist = repository.playlist(id);
    if (playlist == nullptr)
        return {};

    PlaybackQueueResult result;
    std::vector<std::filesystem::path> tracks;

    for (const library::PlaylistEntryRecord& entry : playlist->entries) {
        const library::TrackRecord* media = repository.track(entry.media);
        if (media == nullptr || media->locations.empty()) {
            ++result.unavailable;
            continue;
        }

        tracks.push_back(media->locations.front());
    }

    if (tracks.empty())
        return result;

    PlaybackQueueResult playback = replace_with_paths(session, std::move(tracks));
    playback.unavailable = result.unavailable;

    return playback;
}

} // namespace wiola::app
