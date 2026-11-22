/**
 * @file
 * @brief Incremental Qt table model addressed by stable track identities.
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

#include "track_column.hpp"
#include "track_view_state.hpp"
#include <gui/appearance/artwork_cache.hpp>

#include "track_row.hpp"
#include <library/search.hpp>

#include <QAbstractTableModel>

#include <optional>
#include <vector>

namespace wiola::library {

class LibraryRepository;

}

namespace wiola::gui {

class TrackTableModel final : public QAbstractTableModel {
public:
    static constexpr int standard_columns{StandardTrackColumn::num_columns};

    explicit TrackTableModel(const QWidget& styled, ArtworkCachePolicy policy = {});

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

    void show_rows(const std::vector<TrackRow>& tracks, std::optional<TrackRowId> current = {});
    void show_library(const library::LibraryRepository& repository);
    void show_tag_keys(std::vector<std::string> keys);
    bool set_view_state(TrackViewState state);

    [[nodiscard]] const TrackViewState& view_state() const noexcept { return state_; }

    [[nodiscard]] std::vector<library::MediaId> media_ids() const;
    [[nodiscard]] TrackColumnDescriptor column(int section) const;
    [[nodiscard]] std::optional<TrackRowId> row_id(const QModelIndex& index) const;

    [[nodiscard]] std::size_t total_count() const { return source_rows_.size(); }

private:
    [[nodiscard]] std::vector<TrackRow> visible_rows() const;

    mutable ArtworkCache artwork_;
    ArtworkCachePolicy policy_;
    std::vector<TrackRow> source_rows_;
    std::vector<TrackRow> rows_;
    std::optional<TrackRowId> current_;
    const library::LibraryRepository* repository_{nullptr};
    std::vector<std::string> tag_columns_;
    TrackViewState state_;
};

} // namespace wiola::gui
