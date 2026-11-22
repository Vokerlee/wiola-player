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

    library::RepositoryTransaction transaction{repository_, persist_};
    const library::PlaylistId id = repository_.create_playlist(normalized);

    return transaction.commit() ? id : library::PlaylistId{};
}

bool SavedPlaylistCommands::rename(const library::PlaylistId& id, const std::string& name)
{
    const std::string normalized = normalized_name(name);
    if (normalized.empty() || repository_.playlist(id) == nullptr)
        return false;

    library::RepositoryTransaction transaction{repository_, persist_};
    return repository_.rename_playlist(id, normalized) && transaction.commit();
}

library::PlaylistId SavedPlaylistCommands::duplicate(const library::PlaylistId& id)
{
    const library::PlaylistRecord* source = repository_.playlist(id);
    if (source == nullptr)
        return {};

    library::RepositoryTransaction transaction{repository_, persist_};
    const library::PlaylistRecord copied = *source;
    const library::PlaylistId duplicate = repository_.create_playlist(copied.name + " copy");

    for (const library::PlaylistEntryRecord& entry : copied.entries) {
        if (!repository_.append_track(duplicate, entry.media)) {
            return {};
        }
    }

    return transaction.commit() ? duplicate : library::PlaylistId{};
}

bool SavedPlaylistCommands::remove(const library::PlaylistId& id)
{
    if (repository_.playlist(id) == nullptr)
        return false;

    library::RepositoryTransaction transaction{repository_, persist_};
    return repository_.delete_playlist(id) && transaction.commit();
}

} // namespace wiola::app
