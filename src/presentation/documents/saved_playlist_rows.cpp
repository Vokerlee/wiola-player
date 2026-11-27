/**
 * @file
 * @brief Saved playlist row reconciliation using persistent occurrence identities.
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

#include "saved_playlist_rows.hpp"

namespace wiola::gui {

bool SavedPlaylistRows::reconcile(const library::LibraryRepository& repository,
    const library::PlaylistId& id)
{
    const auto* playlist = repository.playlist(id);

    if (!playlist)
        return false;

    std::vector<TrackRow> rows;
    std::map<library::MediaId, TrackRow> resolved;

    rows.reserve(playlist->entries.size());

    for (const auto& entry : playlist->entries) {
        auto found = resolved.find(entry.media);

        if (found == resolved.end()) {
            TrackRow row;

            if (const auto* record = repository.track(entry.media))
                row = present_record(entry.id, *record);
            else {
                row.media = entry.media;
                row.missing = true;
            }

            found = resolved.emplace(entry.media, std::move(row)).first;
        }

        auto row = found->second;
        row.id = entry.id;
        rows.push_back(std::move(row));
    }

    rows_ = std::move(rows);
    entries_.clear();
    media_.clear();

    for (std::size_t position = 0; position < rows_.size(); ++position) {
        entries_.emplace(std::get<library::PlaylistEntryId>(rows_[position].id), position);
        media_[rows_[position].media].push_back(position);
    }

    return true;
}

bool SavedPlaylistRows::update_media(const library::LibraryRepository& repository,
    const library::MediaId& media)
{
    const auto found = media_.find(media);

    if (found == media_.end())
        return false;

    const auto* record = repository.track(media);

    if (!record)
        return false;

    const auto resolved = present_record(library::PlaylistEntryId{}, *record);
    bool changed = false;

    for (const auto position : found->second) {
        auto row = resolved;
        row.id = rows_[position].id;

        if (row == rows_[position])
            continue;

        rows_[position] = std::move(row);
        changed = true;
    }

    return changed;
}

std::vector<TrackRow> SavedPlaylistRows::media_rows(const library::MediaId& media) const
{
    std::vector<TrackRow> rows;

    if (const auto found = media_.find(media); found != media_.end())
        for (const auto position : found->second)
            rows.push_back(rows_[position]);

    return rows;
}

const TrackRow* SavedPlaylistRows::row(const library::PlaylistEntryId& id) const
{
    const auto found = entries_.find(id);
    return found == entries_.end() ? nullptr : &rows_[found->second];
}

} // namespace wiola::gui
