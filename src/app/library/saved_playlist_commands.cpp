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
#include "playlist_name.hpp"

#include <utility>

namespace wiola::app {

SavedPlaylistCommands::SavedPlaylistCommands(library::InMemoryRepository& repository,
    Persist persist)
    : repository_{repository}
    , persist_{std::make_shared<Persist>(std::move(persist))}
{
}

SavedPlaylistCommands::Persist SavedPlaylistCommands::persistence() const
{
    return [persist = persist_](const library::LibraryRepository& records) {
        return (*persist)(records);
    };
}

PlaylistCreation SavedPlaylistCommands::create(const std::string& name)
{
    const auto normalized = PlaylistName::parse(name);
    if (!normalized)
        return {};

    library::RepositoryTransaction transaction{repository_, persistence()};
    library::PlaylistId id = repository_.create_playlist(normalized->value());

    auto commit = transaction.commit();
    return {commit ? std::move(id) : library::PlaylistId{}, std::move(commit)};
}

library::CommitResult SavedPlaylistCommands::rename(const library::PlaylistId& id,
    const std::string& name)
{
    const auto normalized = PlaylistName::parse(name);
    if (!normalized)
        return false;

    library::RepositoryTransaction transaction{repository_, persistence()};
    if (!repository_.rename_playlist(id, normalized->value()))
        return false;
    return transaction.commit();
}

PlaylistCreation SavedPlaylistCommands::duplicate(const library::PlaylistId& id)
{
    const library::PlaylistRecord* source = repository_.playlist(id);
    if (source == nullptr)
        return {};

    const library::PlaylistRecord copied = *source;
    const auto name = PlaylistName::parse(copied.name + " copy");
    if (!name)
        return {};
    library::RepositoryTransaction transaction{repository_, persistence()};
    library::PlaylistId duplicate = repository_.create_playlist(name->value());

    for (const library::PlaylistEntryRecord& entry : copied.entries) {
        if (!repository_.append_track(duplicate, entry.media)) {
            return {};
        }
    }

    auto commit = transaction.commit();
    return {commit ? std::move(duplicate) : library::PlaylistId{}, std::move(commit)};
}

library::CommitResult SavedPlaylistCommands::remove(const library::PlaylistId& id)
{
    library::RepositoryTransaction transaction{repository_, persistence()};
    if (!repository_.delete_playlist(id))
        return false;
    return transaction.commit();
}

} // namespace wiola::app
