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

#include "track_sort.hpp"
#include <gui/appearance/artwork_cache.hpp>

#include <engine/queue/track.hpp>
#include <library/search.hpp>

#include <QAbstractTableModel>

#include <optional>
#include <vector>

namespace wiola::library {

class LibraryRepository;

}

namespace wiola::gui {

[[nodiscard]] std::vector<std::string>
custom_tag_keys(const library::LibraryRepository& repository);

class TrackTableModel final : public QAbstractTableModel {
public:
    static constexpr int standard_columns{5};

    explicit TrackTableModel(const QWidget& styled, ArtworkCachePolicy policy = {});

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

    void show_tracks(const std::vector<engine::Track>& tracks,
        std::optional<engine::QueueEntryId> current = {});
    void show_library(const library::LibraryRepository& repository);
    void show_tag_keys(std::vector<std::string> keys);
    void sort_by(const std::vector<TrackSortKey>& keys);
    void search_by(const library::SearchQuery& query);
    [[nodiscard]] std::optional<engine::QueueEntryId> track_id(const QModelIndex& index) const;

    [[nodiscard]] std::size_t total_count() const { return source_rows_.size(); }

private:
    [[nodiscard]] std::vector<engine::Track> visible_rows() const;

    mutable ArtworkCache artwork_;
    ArtworkCachePolicy policy_;
    std::vector<engine::Track> source_rows_;
    std::vector<engine::Track> rows_;
    std::optional<engine::QueueEntryId> current_;
    const library::LibraryRepository* repository_{nullptr};
    std::vector<std::string> tag_columns_;
    std::vector<TrackSortKey> sort_keys_;
    library::SearchQuery query_;
};

} // namespace wiola::gui
