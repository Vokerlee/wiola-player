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
#include <gui/appearance/artwork_presentation.hpp>
#include <presentation/tracks/track_view_state.hpp>

#include <library/search.hpp>
#include <presentation/tracks/track_row.hpp>

#include <QAbstractTableModel>
#include <QTimer>

#include <optional>
#include <vector>

namespace wiola::library {

class LibraryRepository;

}

namespace wiola::app {

class SearchIndex;

}

namespace wiola::gui {

class TrackProjection;
struct TrackProjectionInput;

class TrackTableModel final : public QAbstractTableModel {
public:
    static constexpr std::size_t asynchronous_projection_threshold{4096};

    struct ProjectionStatistics {
        std::size_t source_rebuilds{}, source_rows{}, updated_rows{}, queries{}, snapshots{},
            commits{};
    };

    [[nodiscard]] const ProjectionStatistics& projection_statistics() const { return statistics_; }

    [[nodiscard]] bool projection_pending() const { return projection_pending_; }

    using ProjectionAttachment = std::function<void(std::function<void()>)>;

    void on_async_projection(ProjectionAttachment attachment)
    {
        attachment_ = std::move(attachment);
    }

    static constexpr int standard_columns{StandardTrackColumn::num_columns};

    explicit TrackTableModel(const QWidget& styled, ArtworkRequest policy = {});
    ~TrackTableModel() override;

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

    void set_artwork_source(std::function<media::ArtworkReference(const library::MediaId&)> source,
        std::function<void(const library::MediaId&)> request)
    {
        artwork_source_ = std::move(source);
        artwork_request_ = std::move(request);
    }

    void refresh_artwork(const library::MediaId& media);

    void set_search_index(const app::SearchIndex* index)
    {
        search_ = index;
        search_revision_.reset();
        snapshot_.reset();
    }

    void show_rows(const std::vector<TrackRow>& tracks, std::optional<TrackRowId> current = {});
    void set_current(std::optional<TrackRowId> current);
    void update_rows(const std::vector<TrackRow>& changed);
    void show_library(const library::LibraryRepository& repository);

    void bind_library(const library::LibraryRepository& repository)
    {
        repository_ = &repository;
        snapshot_.reset();
    }

    void show_tag_keys(std::vector<std::string> keys);
    bool set_view_state(TrackViewState state);

    [[nodiscard]] const TrackViewState& view_state() const noexcept { return state_; }

    [[nodiscard]] std::vector<library::MediaId> media_ids() const;
    [[nodiscard]] QModelIndex index_of(const TrackRowId& id, int column = 0) const;
    [[nodiscard]] TrackColumnDescriptor column(int section) const;
    [[nodiscard]] std::optional<TrackRowId> row_id(const QModelIndex& index) const;

    [[nodiscard]] std::size_t total_count() const { return source_rows_.size(); }

private:
    [[nodiscard]] std::vector<TrackRow> visible_rows() const;

    void reconcile_rows(std::vector<TrackRow> next);
    void reproject(bool coalesce = false);
    void request_projection();
    bool refresh_search_revision();
    [[nodiscard]] std::shared_ptr<const TrackProjectionInput> projection_input();
    ProjectionStatistics statistics_;
    ProjectionAttachment attachment_;
    std::shared_ptr<const TrackProjectionInput> snapshot_;
    QTimer projection_timer_;
    std::unique_ptr<TrackProjection> projector_;
    std::uint64_t projection_generation_{};
    bool projection_pending_{};
    void rebuild_row_index();
    std::map<TrackRowId, int> row_index_;
    std::map<TrackRowId, std::size_t> source_index_;
    const QWidget& styled_;
    std::shared_ptr<ArtworkPresentationService> artwork_{ArtworkPresentationService::acquire()};
    ArtworkRequest artwork_request_value_;
    std::function<media::ArtworkReference(const library::MediaId&)> artwork_source_;
    std::function<void(const library::MediaId&)> artwork_request_;
    std::map<library::MediaId, std::vector<std::size_t>> source_media_;
    std::vector<TrackRow> source_rows_;
    std::vector<TrackRow> rows_;
    std::optional<TrackRowId> current_;
    const library::LibraryRepository* repository_{nullptr};
    std::vector<std::string> tag_columns_;
    TrackViewState state_;
    const app::SearchIndex* search_{nullptr};
    std::optional<std::uint64_t> search_revision_;
};

} // namespace wiola::gui
