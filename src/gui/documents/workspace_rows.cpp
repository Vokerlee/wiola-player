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
    }
    workspace_ = workspace;
    generation_ = generation;
    accepting_ = true;
    seen_.clear();
    order_.clear();
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
        order_.push_back(entry.media);
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
        if (const auto found = index_.find(entry.media); found != index_.end())
            rows_[found->second] = std::move(row);
        else {
            index_.emplace(entry.media, rows_.size());
            rows_.push_back(std::move(row));
        }
    }
    if (batch.status == app::WorkspaceScanStatus::complete) {
        std::vector<TrackRow> rows;
        rows.reserve(order_.size());
        for (const auto& media : order_)
            rows.push_back(std::move(rows_[index_.at(media)]));
        rows_ = std::move(rows);
        index_rows();
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
    const auto& previous = rows_[found->second];
    if (row.location.empty() || std::ranges::any_of(record->locations, [&](const auto& location) {
            return library::media_id_for(location).value == previous.location_key;
        })) {
        row.location = previous.location;
        row.location_key = previous.location_key;
    }
    if (row == previous)
        return false;
    rows_[found->second] = std::move(row);
    return true;
}

const TrackRow* WorkspaceRows::row(const TrackRowId& id) const
{
    const auto* workspace = std::get_if<WorkspaceTrackId>(&id);
    if (!workspace || workspace->workspace != workspace_.id)
        return nullptr;
    const auto found = index_.find(workspace->media);
    return found == index_.end() ? nullptr : &rows_[found->second];
}

void WorkspaceRows::index_rows()
{
    index_.clear();
    for (std::size_t index = 0; index < rows_.size(); ++index)
        index_.emplace(rows_[index].media, index);
}

} // namespace wiola::gui
