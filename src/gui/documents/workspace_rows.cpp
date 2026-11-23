/**
 * @file
 * @brief Stable workspace rows with explicit scan generations and cached availability.
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

#include "workspace_rows.hpp"
#include <algorithm>
#include <library/location.hpp>

namespace wiola::gui {

bool WorkspaceRows::begin(const library::WorkspaceRecord& workspace, std::uint64_t generation)
{
    if (!workspace.id.valid() || generation == 0 ||
        (workspace.id == workspace_.id && generation <= generation_))
        return false;
    if (workspace.id != workspace_.id) {
        rows_.clear();
        index_.clear();
        ordered_.clear();
        dirty_ = false;
        ++revision_;
    }
    workspace_ = workspace;
    generation_ = generation;
    accepting_ = true;
    seen_.clear();
    return true;
}

bool WorkspaceRows::apply(const app::WorkspaceScanBatch& batch,
    const library::LibraryRepository* repository)
{
    if (!accepting_ || batch.workspace != workspace_.id || batch.generation != generation_)
        return false;
    for (const auto& entry : batch.entries) {
        if (!entry.media.valid() || !seen_.insert(entry.media).second)
            continue;
        TrackRow row;
        const auto* record = repository ? repository->track(entry.media) : nullptr;
        if (record)
            row = present_record(WorkspaceTrackId{workspace_.id, entry.media}, *record);
        row.id = WorkspaceTrackId{workspace_.id, entry.media};
        row.media = entry.media;
        row.location = entry.location;
        row.location_key = library::media_id_for(entry.location).value;
        row.missing = entry.unreadable;
        if (row.metadata.title.empty() && (!record || !record->overrides.title)) {
            const auto title = entry.location.stem().u8string();
            row.metadata.title.assign(title.begin(), title.end());
        }
        static_cast<void>(put(std::move(row)));
    }
    if (batch.status == app::WorkspaceScanStatus::complete) {
        for (auto item = index_.begin(); item != index_.end();) {
            if (!seen_.contains(item->first)) {
                ordered_.erase(item->second);
                item = index_.erase(item);
                dirty_ = true;
                ++revision_;
            } else
                ++item;
        }
    }
    if (batch.status != app::WorkspaceScanStatus::running)
        accepting_ = false;
    return true;
}

bool WorkspaceRows::update_media(const library::LibraryRepository& repository,
    const library::MediaId& media)
{
    const auto found = index_.find(media);
    const auto* record = repository.track(media);
    if (found == index_.end() || !record)
        return false;
    auto row = present_record(WorkspaceTrackId{workspace_.id, media}, *record);
    const auto& previous = found->second->second;
    if (row.location.empty() || std::ranges::any_of(record->locations, [&](const auto& location) {
            return library::media_id_for(location).value == previous.location_key;
        })) {
        row.location = previous.location;
        row.location_key = previous.location_key;
    }
    if (row == previous)
        return false;
    return put(std::move(row));
}

const TrackRow* WorkspaceRows::row(const TrackRowId& id) const
{
    const auto* workspace = std::get_if<WorkspaceTrackId>(&id);
    if (!workspace || workspace->workspace != workspace_.id)
        return nullptr;
    const auto found = index_.find(workspace->media);
    return found == index_.end() ? nullptr : &found->second->second;
}

bool WorkspaceRows::put(TrackRow row)
{
    const auto found = index_.find(row.media);
    if (found != index_.end() && found->second->second == row)
        return false;
    const auto key = std::pair{row.location_key, row.media};
    auto [inserted, fresh] = ordered_.insert_or_assign(key, std::move(row));
    if (found != index_.end()) {
        if (found->second != inserted)
            ordered_.erase(found->second);
        found->second = inserted;
    } else
        index_.emplace(inserted->second.media, inserted);
    dirty_ = true;
    ++revision_;
    return true;
}

const std::vector<TrackRow>& WorkspaceRows::rows() const
{
    if (dirty_) {
        rows_.clear();
        rows_.reserve(ordered_.size());
        for (const auto& [key, row] : ordered_)
            rows_.push_back(row);
        dirty_ = false;
    }
    return rows_;
}

} // namespace wiola::gui
