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
#include "repository_transaction.hpp"
#include "tags.hpp"

#include <algorithm>
#include <set>
#include <utility>

namespace wiola::library {

void InMemoryRepository::check_mutation() const
{
    if (transaction_)
        transaction_->check_mutation();
}

bool InMemoryRepository::put_track(TrackRecord track)
{
    check_mutation();

    if (!track.id.valid() || track.tags.size() > max_tags_per_track)
        return false;

    std::set<std::pair<std::string, std::string>> values;
    std::set<TagId> identities;

    for (const TagRecord& tag : track.tags) {
        const auto candidate = make_tag_value(tag.key, tag.value);
        const auto normalized =
            std::pair{normalized_tag_text(tag.key), normalized_tag_text(tag.value)};

        if (!tag.id.valid() || !candidate || candidate->key != tag.key ||
            candidate->value != tag.value || !identities.insert(tag.id).second ||
            !values.insert(normalized).second)
            return false;
    }

    const MediaId id{track.id};

    if (transaction_)
        transaction_->touch_track(id);

    data_.tracks_.insert_or_assign(id, std::move(track));

    return true;
}

const TrackRecord* InMemoryRepository::track(const MediaId& id) const
{
    const auto found{data_.tracks_.find(id)};

    return found == data_.tracks_.end() ? nullptr : &found->second;
}

std::vector<TrackRecord> InMemoryRepository::tracks() const
{
    std::vector<TrackRecord> result;
    result.reserve(data_.tracks_.size());

    for (const auto& [id, track] : data_.tracks_)
        result.push_back(track);

    return result;
}

bool InMemoryRepository::replace_all(const std::vector<TrackRecord>& tracks,
    const std::vector<PlaylistRecord>& playlists, const std::vector<WorkspaceRecord>& workspaces)
{
    check_mutation();

    InMemoryRepository replacement;

    for (const TrackRecord& track : tracks) {
        if (replacement.data_.tracks_.contains(track.id) || !replacement.put_track(track))
            return false;
    }

    for (const PlaylistRecord& playlist : playlists) {
        if (replacement.data_.playlists_.contains(playlist.id) ||
            !replacement.put_playlist(playlist))
            return false;
    }

    for (const WorkspaceRecord& workspace : workspaces) {
        if (replacement.data_.workspaces_.contains(workspace.id) ||
            !replacement.put_workspace(workspace))
            return false;
    }

    if (transaction_) {
        const auto remember = [&](const auto& current, const auto& next, auto touch) {
            for (auto next_record = current.begin(); next_record != current.end();) {
                const auto id = (next_record++)->first;
                (transaction_->*touch)(id);
            }
            for (const auto& [id, record] : next)
                (transaction_->*touch)(id);
        };
        remember(data_.tracks_, replacement.data_.tracks_, &RepositoryTransaction::touch_track);
        remember(data_.playlists_, replacement.data_.playlists_,
            &RepositoryTransaction::touch_playlist);
        remember(data_.workspaces_, replacement.data_.workspaces_,
            &RepositoryTransaction::touch_workspace);
    }

    data_.tracks_ = std::move(replacement.data_.tracks_);
    data_.playlists_ = std::move(replacement.data_.playlists_);
    data_.workspaces_ = std::move(replacement.data_.workspaces_);

    return true;
}

PlaylistId InMemoryRepository::create_playlist(std::string name)
{
    check_mutation();

    const PlaylistId id{next_playlist_id()};

    if (transaction_)
        transaction_->touch_playlist(id);

    data_.playlists_.emplace(id, PlaylistRecord{.id = id, .name = std::move(name), .entries = {}});

    return id;
}

bool InMemoryRepository::put_playlist(PlaylistRecord playlist)
{
    check_mutation();

    if (!playlist.id.valid())
        return false;

    std::set<PlaylistEntryId> entries;

    for (const PlaylistEntryRecord& entry : playlist.entries) {
        if (!entry.id.valid() || track(entry.media) == nullptr || !entries.insert(entry.id).second)
            return false;
    }

    const PlaylistId id{playlist.id};

    if (transaction_)
        transaction_->touch_playlist(id);

    data_.playlists_.insert_or_assign(id, std::move(playlist));

    return true;
}

bool InMemoryRepository::rename_playlist(const PlaylistId& id, std::string name)
{
    check_mutation();

    if (transaction_)
        transaction_->touch_playlist(id);

    PlaylistRecord* found{data_.playlists_.contains(id) ? &data_.playlists_.at(id) : nullptr};

    if (found == nullptr)
        return false;

    found->name = std::move(name);

    return true;
}

bool InMemoryRepository::delete_playlist(const PlaylistId& id)
{
    check_mutation();

    if (transaction_)
        transaction_->touch_playlist(id);

    return data_.playlists_.erase(id) != 0;
}

const PlaylistRecord* InMemoryRepository::playlist(const PlaylistId& id) const
{
    const auto found{data_.playlists_.find(id)};

    return found == data_.playlists_.end() ? nullptr : &found->second;
}

std::vector<PlaylistRecord> InMemoryRepository::playlists() const
{
    std::vector<PlaylistRecord> result;
    result.reserve(data_.playlists_.size());

    for (const auto& [id, playlist] : data_.playlists_)
        result.push_back(playlist);

    return result;
}

std::optional<PlaylistEntryId> InMemoryRepository::append_track(const PlaylistId& playlist_id,
    const MediaId& media)
{
    check_mutation();

    if (transaction_)
        transaction_->touch_playlist(playlist_id);

    PlaylistRecord* found{
        data_.playlists_.contains(playlist_id) ? &data_.playlists_.at(playlist_id) : nullptr};

    if (found == nullptr || track(media) == nullptr)
        return std::nullopt;

    const PlaylistEntryId id{next_entry_id()};
    found->entries.push_back(PlaylistEntryRecord{.id = id, .media = media});

    return id;
}

bool InMemoryRepository::remove_entry(const PlaylistId& playlist_id, const PlaylistEntryId& entry)
{
    check_mutation();

    if (transaction_)
        transaction_->touch_playlist(playlist_id);

    PlaylistRecord* found{
        data_.playlists_.contains(playlist_id) ? &data_.playlists_.at(playlist_id) : nullptr};

    if (found == nullptr)
        return false;

    return std::erase_if(found->entries, [&entry](const PlaylistEntryRecord& candidate) {
        return candidate.id == entry;
    }) != 0;
}

WorkspaceId InMemoryRepository::create_workspace(std::filesystem::path root, bool recursive)
{
    check_mutation();

    if (root.empty())
        return {};

    const WorkspaceId id{next_workspace_id()};

    if (transaction_)
        transaction_->touch_workspace(id);

    data_.workspaces_.emplace(id,
        WorkspaceRecord{.id = id, .root = root.lexically_normal(), .recursive = recursive});

    return id;
}

bool InMemoryRepository::put_workspace(WorkspaceRecord workspace)
{
    check_mutation();

    if (!workspace.id.valid() || workspace.root.empty())
        return false;

    workspace.root = workspace.root.lexically_normal();

    if (transaction_)
        transaction_->touch_workspace(workspace.id);

    data_.workspaces_.insert_or_assign(workspace.id, std::move(workspace));

    return true;
}

const WorkspaceRecord* InMemoryRepository::workspace(const WorkspaceId& id) const
{
    const auto found = data_.workspaces_.find(id);
    return found == data_.workspaces_.end() ? nullptr : &found->second;
}

std::vector<WorkspaceRecord> InMemoryRepository::workspaces() const
{
    std::vector<WorkspaceRecord> result;
    result.reserve(data_.workspaces_.size());

    for (const auto& [id, workspace] : data_.workspaces_)
        result.push_back(workspace);

    return result;
}

bool InMemoryRepository::delete_workspace(const WorkspaceId& id)
{
    check_mutation();

    if (transaction_)
        transaction_->touch_workspace(id);

    return data_.workspaces_.erase(id) != 0;
}

TagId InMemoryRepository::add_tag(const MediaId& media, std::string key, std::string value)
{
    check_mutation();

    if (transaction_)
        transaction_->touch_track(media);

    TrackRecord* found{data_.tracks_.contains(media) ? &data_.tracks_.at(media) : nullptr};
    auto candidate = make_tag_value(std::move(key), std::move(value));

    if (found == nullptr || !candidate || found->tags.size() >= max_tags_per_track)
        return {};

    if (contains_tag_value(found->tags, *candidate))
        return {};

    const TagId id{next_tag_id()};
    found->tags.push_back(
        TagRecord{.id = id, .key = std::move(candidate->key), .value = std::move(candidate->value)}
    );
    ++found->metadata_revision;

    return id;
}

bool InMemoryRepository::remove_tag(const MediaId& media, const TagId& tag)
{
    check_mutation();

    if (transaction_)
        transaction_->touch_track(media);

    TrackRecord* found{data_.tracks_.contains(media) ? &data_.tracks_.at(media) : nullptr};

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
        id.value = "playlist-" + std::to_string(data_.next_playlist_++);
    } while (data_.playlists_.contains(id));

    return id;
}

PlaylistEntryId InMemoryRepository::next_entry_id()
{
    PlaylistEntryId id;

    do {
        id.value = "entry-" + std::to_string(data_.next_entry_++);
    } while (entry_exists(id));

    return id;
}

WorkspaceId InMemoryRepository::next_workspace_id()
{
    WorkspaceId id;

    do {
        id.value = "workspace-" + std::to_string(data_.next_workspace_++);
    } while (data_.workspaces_.contains(id));

    return id;
}

TagId InMemoryRepository::next_tag_id()
{
    TagId id;

    do {
        id.value = "tag-" + std::to_string(data_.next_tag_++);
    } while (tag_exists(id));

    return id;
}

bool InMemoryRepository::entry_exists(const PlaylistEntryId& id) const
{
    return std::ranges::any_of(data_.playlists_, [&id](const auto& pair) {
        return std::ranges::any_of(pair.second.entries,
            [&id](const PlaylistEntryRecord& entry) { return entry.id == id; });
    });
}

bool InMemoryRepository::tag_exists(const TagId& id) const
{
    return std::ranges::any_of(data_.tracks_, [&id](const auto& pair) {
        return std::ranges::any_of(pair.second.tags,
            [&id](const TagRecord& tag) { return tag.id == id; });
    });
}

} // namespace wiola::library
