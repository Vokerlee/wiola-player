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

#pragma once
#include <app/workspace/workspace_scanner.hpp>
#include <gui/tracks/track_row.hpp>
#include <library/repository.hpp>
#include <map>
#include <set>

namespace wiola::gui {

class WorkspaceRows final {
public:
    bool begin(const library::WorkspaceRecord& workspace, std::uint64_t generation);
    bool apply(const app::WorkspaceScanBatch& batch, const library::LibraryRepository* repository);
    bool update_media(const library::LibraryRepository& repository, const library::MediaId& media);

    void cancel() noexcept { accepting_ = false; }

    [[nodiscard]] const std::vector<TrackRow>& rows() const noexcept { return rows_; }

    [[nodiscard]] const TrackRow* row(const TrackRowId& id) const;

private:
    void index_rows();
    library::WorkspaceRecord workspace_;
    std::uint64_t generation_{0};
    bool accepting_{false};
    std::vector<TrackRow> rows_;
    std::map<library::MediaId, std::size_t> index_;
    std::set<library::MediaId> seen_;
};

} // namespace wiola::gui
