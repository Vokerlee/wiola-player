/**
 * @file
 * @brief Deterministic in-memory library storage.
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

#include "in_memory_repository.hpp"

#include <algorithm>
#include <set>
#include <utility>

namespace wiola::library {

namespace {

constexpr std::size_t max_tags_per_track{256};

}

bool InMemoryRepository::put_track(TrackRecord track)
{
    if (!track.id.valid())
        return false;

    const MediaId id{track.id};
    tracks_.insert_or_assign(id, std::move(track));

    return true;
}

const TrackRecord* InMemoryRepository::track(const MediaId& id) const
{
    const auto found{tracks_.find(id)};

    return found == tracks_.end() ? nullptr : &found->second;
}

std::vector<TrackRecord> InMemoryRepository::tracks() const
{
    std::vector<TrackRecord> result;
    result.reserve(tracks_.size());

    for (const auto& [id, track] : tracks_)
        result.push_back(track);

    return result;
}

bool InMemoryRepository::replace_all(const std::vector<TrackRecord>& tracks,
    const std::vector<PlaylistRecord>& playlists, const std::vector<WorkspaceRecord>& workspaces)
{
    InMemoryRepository replacement;

    for (const TrackRecord& track : tracks) {
        if (replacement.tracks_.contains(track.id) || !replacement.put_track(track))
            return false;
    }

    for (const PlaylistRecord& playlist : playlists) {
        if (replacement.playlists_.contains(playlist.id) || !replacement.put_playlist(playlist))
            return false;
    }

    for (const WorkspaceRecord& workspace : workspaces) {
        if (replacement.workspaces_.contains(workspace.id) || !replacement.put_workspace(workspace))
            return false;
    }

    tracks_ = std::move(replacement.tracks_);
    playlists_ = std::move(replacement.playlists_);
    workspaces_ = std::move(replacement.workspaces_);

    return true;
}

PlaylistId InMemoryRepository::create_playlist(std::string name)
{
    const PlaylistId id{next_playlist_id()};
    playlists_.emplace(id, PlaylistRecord{.id = id, .name = std::move(name), .entries = {}});

    return id;
}

bool InMemoryRepository::put_playlist(PlaylistRecord playlist)
{
    if (!playlist.id.valid())
        return false;

    std::set<PlaylistEntryId> entries;

    for (const PlaylistEntryRecord& entry : playlist.entries) {
        if (!entry.id.valid() || track(entry.media) == nullptr || !entries.insert(entry.id).second)
            return false;
    }

    const PlaylistId id{playlist.id};
    playlists_.insert_or_assign(id, std::move(playlist));

    return true;
}

bool InMemoryRepository::rename_playlist(const PlaylistId& id, std::string name)
{
    PlaylistRecord* found{playlists_.contains(id) ? &playlists_.at(id) : nullptr};

    if (found == nullptr)
        return false;

    found->name = std::move(name);

    return true;
}

bool InMemoryRepository::delete_playlist(const PlaylistId& id)
{
    return playlists_.erase(id) != 0;
}

const PlaylistRecord* InMemoryRepository::playlist(const PlaylistId& id) const
{
    const auto found{playlists_.find(id)};

    return found == playlists_.end() ? nullptr : &found->second;
}

std::vector<PlaylistRecord> InMemoryRepository::playlists() const
{
    std::vector<PlaylistRecord> result;
    result.reserve(playlists_.size());

    for (const auto& [id, playlist] : playlists_)
        result.push_back(playlist);

    return result;
}

std::optional<PlaylistEntryId> InMemoryRepository::append_track(const PlaylistId& playlist_id,
    const MediaId& media)
{
    PlaylistRecord* found{playlists_.contains(playlist_id) ? &playlists_.at(playlist_id) : nullptr};

    if (found == nullptr || track(media) == nullptr)
        return std::nullopt;

    const PlaylistEntryId id{next_entry_id()};
    found->entries.push_back(PlaylistEntryRecord{.id = id, .media = media});

    return id;
}

bool InMemoryRepository::remove_entry(const PlaylistId& playlist_id, const PlaylistEntryId& entry)
{
    PlaylistRecord* found{playlists_.contains(playlist_id) ? &playlists_.at(playlist_id) : nullptr};

    if (found == nullptr)
        return false;

    return std::erase_if(found->entries, [&entry](const PlaylistEntryRecord& candidate) {
        return candidate.id == entry;
    }) != 0;
}

WorkspaceId InMemoryRepository::create_workspace(std::filesystem::path root, bool recursive)
{
    if (root.empty())
        return {};

    const WorkspaceId id{next_workspace_id()};
    workspaces_.emplace(id,
        WorkspaceRecord{.id = id,
            .root = std::filesystem::absolute(root).lexically_normal(),
            .recursive = recursive});

    return id;
}

bool InMemoryRepository::put_workspace(WorkspaceRecord workspace)
{
    if (!workspace.id.valid() || workspace.root.empty())
        return false;

    workspace.root = std::filesystem::absolute(workspace.root).lexically_normal();
    workspaces_.insert_or_assign(workspace.id, std::move(workspace));

    return true;
}

const WorkspaceRecord* InMemoryRepository::workspace(const WorkspaceId& id) const
{
    const auto found = workspaces_.find(id);
    return found == workspaces_.end() ? nullptr : &found->second;
}

std::vector<WorkspaceRecord> InMemoryRepository::workspaces() const
{
    std::vector<WorkspaceRecord> result;
    result.reserve(workspaces_.size());

    for (const auto& [id, workspace] : workspaces_)
        result.push_back(workspace);

    return result;
}

bool InMemoryRepository::delete_workspace(const WorkspaceId& id)
{
    return workspaces_.erase(id) != 0;
}

TagId InMemoryRepository::add_tag(const MediaId& media, std::string key, std::string value)
{
    TrackRecord* found{tracks_.contains(media) ? &tracks_.at(media) : nullptr};

    if (found == nullptr || found->tags.size() >= max_tags_per_track)
        return {};

    const auto duplicate = std::ranges::find_if(found->tags,
        [&key, &value](const TagRecord& tag) { return tag.key == key && tag.value == value; });

    if (duplicate != found->tags.end())
        return {};

    const TagId id{next_tag_id()};
    found->tags.push_back(TagRecord{.id = id, .key = std::move(key), .value = std::move(value)});
    ++found->metadata_revision;

    return id;
}

bool InMemoryRepository::remove_tag(const MediaId& media, const TagId& tag)
{
    TrackRecord* found{tracks_.contains(media) ? &tracks_.at(media) : nullptr};

    if (found == nullptr)
        return false;

    const bool removed{std::erase_if(found->tags, [&tag](const TagRecord& candidate) {
        return candidate.id == tag;
    }) != 0};

    if (removed)
        ++found->metadata_revision;

    return removed;
}

PlaylistId InMemoryRepository::next_playlist_id()
{
    PlaylistId id;

    do {
        id.value = "playlist-" + std::to_string(next_playlist_++);
    } while (playlists_.contains(id));

    return id;
}

PlaylistEntryId InMemoryRepository::next_entry_id()
{
    PlaylistEntryId id;

    do {
        id.value = "entry-" + std::to_string(next_entry_++);
    } while (entry_exists(id));

    return id;
}

WorkspaceId InMemoryRepository::next_workspace_id()
{
    WorkspaceId id;

    do {
        id.value = "workspace-" + std::to_string(next_workspace_++);
    } while (workspaces_.contains(id));

    return id;
}

TagId InMemoryRepository::next_tag_id()
{
    TagId id;

    do {
        id.value = "tag-" + std::to_string(next_tag_++);
    } while (tag_exists(id));

    return id;
}

bool InMemoryRepository::entry_exists(const PlaylistEntryId& id) const
{
    return std::ranges::any_of(playlists_, [&id](const auto& pair) {
        return std::ranges::any_of(pair.second.entries,
            [&id](const PlaylistEntryRecord& entry) { return entry.id == id; });
    });
}

bool InMemoryRepository::tag_exists(const TagId& id) const
{
    return std::ranges::any_of(tracks_, [&id](const auto& pair) {
        return std::ranges::any_of(pair.second.tags,
            [&id](const TagRecord& tag) { return tag.id == id; });
    });
}

} // namespace wiola::library
