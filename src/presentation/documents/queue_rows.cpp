/**
 * @file
 * @brief Explicit playback-queue conversion at the Now Playing presentation boundary.
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

#include "queue_rows.hpp"
#include <algorithm>
#include <library/location.hpp>
#include <map>
#include <media/local/resource.hpp>
#include <set>

namespace wiola::gui {

TrackRow queue_row(const engine::QueueEntry& track)
{
    TrackRow row;
    row.id = track.id();
    if (auto path = media::local_path(track.resource())) {
        row.media = library::media_id_for(*path);
        row.location = std::move(*path);
        row.location_key = row.media.value;
    }
    row.metadata = track.metadata();
    row.artwork = track.artwork();
    row.metadata_revision = track.revision();
    return row;
}

std::vector<TrackRow> queue_rows(std::span<const engine::QueueEntry> tracks)
{
    std::vector<TrackRow> rows;
    rows.reserve(tracks.size());
    for (const auto& track : tracks)
        rows.push_back(queue_row(track));
    return rows;
}

std::vector<engine::QueueEntryId> queue_entry_ids(std::span<const TrackRowId> rows)
{
    std::vector<engine::QueueEntryId> result;
    result.reserve(rows.size());
    for (const auto& row : rows)
        if (const auto* id = std::get_if<engine::QueueEntryId>(&row))
            result.push_back(*id);
    return result;
}

std::optional<engine::QueueEntryId> queue_entry_id(const std::optional<TrackRowId>& row)
{
    if (row)
        if (const auto* id = std::get_if<engine::QueueEntryId>(&*row))
            return *id;
    return {};
}

TrackRow QueueRows::present(const engine::QueueEntry& track,
    const library::LibraryRepository* repository) const
{
    auto row = queue_row(track);
    if (resolve_media_)
        row.media = resolve_media_(row.location);
    if (const auto* record = repository ? repository->track(row.media) : nullptr) {
        TrackRow resolved;
        if (record->embedded == media::TrackMetadata{}) {
            auto cached = *record;
            cached.embedded = track.metadata();
            resolved = present_record(row.id, cached);
        } else
            resolved = present_record(row.id, *record);
        resolved.location = row.location;
        resolved.location_key = row.location_key;
        resolved.artwork = row.artwork;
        if (!resolved.metadata.duration)
            resolved.metadata.duration = row.metadata.duration;
        row = std::move(resolved);
    }
    return row;
}

void QueueRows::replace(std::size_t position, TrackRow row)
{
    const auto id = std::get<engine::QueueEntryId>(row.id);
    auto& previous = rows_[position];
    if (previous.media != row.media) {
        const auto found = media_.find(previous.media);
        if (found != media_.end()) {
            std::erase(found->second, id);
            if (found->second.empty())
                media_.erase(found);
        }
        media_[row.media].push_back(id);
    }
    if (previous.location_key != row.location_key) {
        const auto found = locations_.find(previous.location_key);
        if (found != locations_.end()) {
            std::erase(found->second, id);
            if (found->second.empty())
                locations_.erase(found);
        }
        locations_[row.location_key].push_back(id);
    }
    previous = std::move(row);
}

QueueRowChanges QueueRows::reconcile(const engine::QueueView& playlist,
    const library::LibraryRepository* repository)
{
    QueueRowChanges changes;
    const auto* selected = playlist.current();
    const auto current = selected ? std::optional{selected->id()} : std::nullopt;
    changes.current = current_ != current;
    current_ = current;
    if (playlist_ != &playlist || structure_ != playlist.structural_revision()) {
        playlist_ = &playlist;
        structure_ = playlist.structural_revision();
        metadata_ = playlist.metadata_revision();
        rows_.clear();
        revisions_.clear();
        entries_.clear();
        media_.clear();
        locations_.clear();
        const auto tracks = playlist.tracks();
        rows_.reserve(tracks.size());
        revisions_.reserve(tracks.size());
        for (const auto& track : tracks) {
            auto row = present(track, repository);
            entries_.emplace(track.id(), rows_.size());
            media_[row.media].push_back(track.id());
            locations_[row.location_key].push_back(track.id());
            rows_.push_back(std::move(row));
            revisions_.push_back(track.revision());
        }
        changes.structure = true;
        return changes;
    }
    if (metadata_ != playlist.metadata_revision()) {
        metadata_ = playlist.metadata_revision();
        const auto tracks = playlist.tracks();
        for (std::size_t i = 0; i < rows_.size(); ++i)
            if (revisions_[i] != tracks[i].revision()) {
                revisions_[i] = tracks[i].revision();
                auto row = present(tracks[i], repository);
                if (row != rows_[i]) {
                    replace(i, std::move(row));
                    changes.rows.push_back(rows_[i]);
                }
            }
    }
    return changes;
}

QueueRowChanges QueueRows::apply_library(const engine::QueueView& playlist,
    const library::LibraryRepository& repository, const library::LibraryChangeBatch& batch)
{
    auto changes = reconcile(playlist, &repository);
    changes.playback_changed = changes.changed();
    const auto changed = update_library(playlist, repository, batch);
    if (!changes.structure) {
        std::map<TrackRowId, TrackRow> patches;
        for (auto& row : changes.rows)
            patches.insert_or_assign(row.id, std::move(row));
        for (const auto& row : changed)
            patches.insert_or_assign(row.id, row);
        changes.rows.clear();
        for (auto& [id, row] : patches)
            changes.rows.push_back(std::move(row));
    }
    return changes;
}

std::vector<TrackRow> QueueRows::update_library(const engine::QueueView& playlist,
    const library::LibraryRepository& repository, const library::LibraryChangeBatch& batch)
{
    std::set<engine::QueueEntryId> affected;
    const auto add_media = [&](const library::MediaId& id) {
        if (const auto found = media_.find(id); found != media_.end())
            affected.insert(found->second.begin(), found->second.end());
    };
    for (const auto& change : batch.media) {
        add_media(change.media);
        if (change.includes(library::MediaChangeFields::locations))
            if (const auto* record = repository.track(change.media))
                for (const auto& path : record->locations) {
                    if (const auto found = locations_.find(library::media_id_for(path).value);
                        found != locations_.end())
                        affected.insert(found->second.begin(), found->second.end());
                }
    }
    for (const auto& id : batch.removed_media)
        add_media(id);
    std::vector<TrackRow> changed;
    const auto tracks = playlist.tracks();
    for (const auto id : affected)
        if (const auto found = entries_.find(id); found != entries_.end()) {
            const auto position = found->second;
            auto row = present(tracks[position], &repository);
            if (row != rows_[position]) {
                replace(position, std::move(row));
                changed.push_back(rows_[position]);
            }
        }
    return changed;
}

} // namespace wiola::gui
