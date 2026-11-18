/**
 * @file
 * @brief Transactional create, rename, duplicate, and delete playlist commands.
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

#include "saved_playlist_commands.hpp"

#include <QString>

#include <utility>

namespace wiola::app {

namespace {

std::string normalized_name(const std::string& name)
{
    const QString normalized = QString::fromStdString(name).trimmed();
    return normalized.isEmpty() || normalized.size() > 256 ? std::string{}
                                                           : normalized.toStdString();
}

} // namespace

SavedPlaylistCommands::SavedPlaylistCommands(library::InMemoryRepository& repository,
    Persist persist)
    : repository_{repository}
    , persist_{std::move(persist)}
{
}

library::PlaylistId SavedPlaylistCommands::create(const std::string& name)
{
    const std::string normalized = normalized_name(name);
    if (normalized.empty())
        return {};

    const Snapshot previous = snapshot();
    const library::PlaylistId id = repository_.create_playlist(normalized);

    return commit(previous) ? id : library::PlaylistId{};
}

bool SavedPlaylistCommands::rename(const library::PlaylistId& id, const std::string& name)
{
    const std::string normalized = normalized_name(name);
    if (normalized.empty() || repository_.playlist(id) == nullptr)
        return false;

    const Snapshot previous = snapshot();
    return repository_.rename_playlist(id, normalized) && commit(previous);
}

library::PlaylistId SavedPlaylistCommands::duplicate(const library::PlaylistId& id)
{
    const library::PlaylistRecord* source = repository_.playlist(id);
    if (source == nullptr)
        return {};

    const Snapshot previous = snapshot();
    const library::PlaylistRecord copied = *source;
    const library::PlaylistId duplicate = repository_.create_playlist(copied.name + " copy");

    for (const library::PlaylistEntryRecord& entry : copied.entries) {
        if (!repository_.append_track(duplicate, entry.media)) {
            static_cast<void>(repository_.replace_all(previous.tracks, previous.playlists));
            return {};
        }
    }

    return commit(previous) ? duplicate : library::PlaylistId{};
}

bool SavedPlaylistCommands::remove(const library::PlaylistId& id)
{
    if (repository_.playlist(id) == nullptr)
        return false;

    const Snapshot previous = snapshot();
    return repository_.delete_playlist(id) && commit(previous);
}

SavedPlaylistCommands::Snapshot SavedPlaylistCommands::snapshot() const
{
    return {.tracks = repository_.tracks(), .playlists = repository_.playlists()};
}

bool SavedPlaylistCommands::commit(const Snapshot& previous)
{
    if (persist_(repository_))
        return true;

    static_cast<void>(repository_.replace_all(previous.tracks, previous.playlists));

    return false;
}

} // namespace wiola::app
