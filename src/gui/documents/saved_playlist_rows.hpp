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

#pragma once
#include <gui/tracks/track_row.hpp>
#include <library/repository.hpp>
#include <map>

namespace wiola::gui {

class SavedPlaylistRows final {
public:
    bool reconcile(const library::LibraryRepository& repository, const library::PlaylistId& id);
    bool update_media(const library::LibraryRepository& repository, const library::MediaId& media);

    [[nodiscard]] const std::vector<TrackRow>& rows() const noexcept { return rows_; }

    [[nodiscard]] const TrackRow* row(const library::PlaylistEntryId& id) const;

private:
    std::vector<TrackRow> rows_;
    std::map<library::PlaylistEntryId, std::size_t> entries_;
    std::map<library::MediaId, std::vector<std::size_t>> media_;
};

} // namespace wiola::gui
