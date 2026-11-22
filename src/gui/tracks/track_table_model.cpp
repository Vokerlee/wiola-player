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

#include "track_table_model.hpp"

#include "track_view_pipeline.hpp"
#include <gui/controls/clock.hpp>
#include <platform/qt/qt_path_adapter.hpp>

#include <library/location.hpp>
#include <library/repository.hpp>
#include <library/tags.hpp>

#include <QColor>
#include <QFont>
#include <QStringList>

#include <algorithm>
#include <array>
#include <map>

namespace wiola::gui {

namespace {

using Column = StandardTrackColumn;

bool same_metadata(const TrackRow& a, const TrackRow& b)
{
    return a == b;
}

bool restricted(const library::SearchQuery& query)
{
    return track_query_active(query);
}

} // namespace

TrackTableModel::TrackTableModel(const QWidget& styled, ArtworkCachePolicy policy)
    : artwork_{styled, policy}
    , policy_{policy}
{
}

int TrackTableModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

int TrackTableModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : standard_columns + static_cast<int>(tag_columns_.size());
}

QVariant TrackTableModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 ||
        static_cast<std::size_t>(index.row()) >= rows_.size())
        return {};

    const TrackRow& track{rows_[static_cast<std::size_t>(index.row())]};

    if (role == Qt::ToolTipRole)
        return platform::qt::QtPathAdapter::to_qt(track.location) +
            (track.missing ? " — unavailable" : "");

    if (role == Qt::DisplayRole && index.column() >= standard_columns) {
        const std::size_t column = static_cast<std::size_t>(index.column() - standard_columns);
        if (column >= tag_columns_.size())
            return {};

        const library::TrackRecord* record =
            repository_ ? repository_->track(track.media) : nullptr;

        const std::string key = library::normalized_tag_text(tag_columns_[column]);
        QStringList values;

        for (const library::TagRecord& tag : record ? record->tags : track.tags) {
            if (library::normalized_tag_text(tag.key) == key)
                values.push_back(QString::fromStdString(tag.value));
        }

        return values.join(", ");
    }

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case Column::title:
            return QString::fromStdString(track.metadata.title);
        case Column::artist:
            return QString::fromStdString(track.metadata.artist);
        case Column::album:
            return QString::fromStdString(track.metadata.album);
        case Column::length:
            return track.metadata.duration ? as_clock(*track.metadata.duration) : QString{};
        default:
            return {};
        }
    }

    if (role == Qt::DecorationRole && index.column() == Column::cover) {
        return artwork_.cover_of({.media = track.media.value, .revision = track.artwork_revision},
            track.artwork);
    }

    if (current_ != track.id)
        return {};

    if (role == Qt::FontRole) {
        QFont bold;
        bold.setBold(true);
        return bold;
    }

    if (role == Qt::BackgroundRole && index.column() != Column::cover) {
        QColor marked{109, 159, 220};
        marked.setAlpha(40);
        return marked;
    }

    return {};
}

QVariant TrackTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};

    return section >= 0 && section < columnCount() ? QString::fromStdString(column(section).label)
                                                   : QVariant{};
}

std::vector<library::MediaId> TrackTableModel::media_ids() const
{
    std::vector<library::MediaId> result;
    for (const auto& row : source_rows_)
        result.push_back(row.media);
    return result;
}

TrackColumnDescriptor TrackTableModel::column(int section) const
{
    if (section >= standard_columns && section < columnCount())
        return tag_track_column(tag_columns_[static_cast<std::size_t>(section - standard_columns)]);
    return standard_track_columns().at(static_cast<std::size_t>(section));
}

void TrackTableModel::show_rows(const std::vector<TrackRow>& tracks,
    std::optional<TrackRowId> current)
{
    const bool same_structure = source_rows_.size() == tracks.size() &&
        std::ranges::equal(source_rows_, tracks,
            [](const TrackRow& a, const TrackRow& b) { return a.id == b.id; });
    const bool same_contents = same_structure &&
        std::ranges::equal(source_rows_, tracks,
            [](const TrackRow& a, const TrackRow& b) { return same_metadata(a, b); });

    if (!state_.sort_keys.empty() || restricted(state_.query)) {
        if (!same_contents) {
            beginResetModel();
            source_rows_ = tracks;
            rows_ = visible_rows();
            endResetModel();
        }
    } else if (!same_structure) {
        beginResetModel();
        source_rows_ = tracks;
        rows_ = tracks;
        endResetModel();
    } else {
        for (std::size_t row = 0; row < rows_.size(); ++row) {
            if (same_metadata(source_rows_[row], tracks[row]))
                continue;

            source_rows_[row] = tracks[row];
            rows_[row] = tracks[row];
            const QModelIndex first{index(static_cast<int>(row), 0)};
            const QModelIndex last{index(static_cast<int>(row), columnCount({}) - 1)};
            emit dataChanged(first, last);
        }
    }

    const auto previous{current_};
    current_ = current;

    for (const auto& id : {previous, current_}) {
        if (!id)
            continue;

        const auto row = std::ranges::find(rows_, *id, &TrackRow::id);
        if (row != rows_.end()) {
            const auto position = static_cast<int>(std::distance(rows_.begin(), row));
            emit dataChanged(index(position, 0), index(position, columnCount({}) - 1));
        }
    }
}

void TrackTableModel::show_library(const library::LibraryRepository& repository)
{
    repository_ = &repository;
    std::vector<TrackRow> visible = visible_rows();
    const bool different_order = rows_.size() != visible.size() ||
        !std::ranges::equal(rows_, visible,
            [](const TrackRow& a, const TrackRow& b) { return a.id == b.id; });

    if (different_order) {
        beginResetModel();
        rows_ = std::move(visible);
        endResetModel();
    } else {
        rows_ = std::move(visible);
    }

    if (rows_.empty() || tag_columns_.empty())
        return;

    emit dataChanged(index(0, standard_columns),
        index(static_cast<int>(rows_.size()) - 1, columnCount({}) - 1));
}

void TrackTableModel::show_tag_keys(std::vector<std::string> keys)
{
    std::vector<std::string> columns;

    for (const std::string& key : keys) {
        const auto valid = library::make_tag_value(key, "value");
        if (!valid)
            continue;

        const std::string normalized = library::normalized_tag_text(valid->key);
        const bool duplicate = std::ranges::any_of(columns, [&](const std::string& present) {
            return library::normalized_tag_text(present) == normalized;
        });

        if (!duplicate)
            columns.push_back(valid->key);
    }

    if (columns == tag_columns_)
        return;

    beginResetModel();
    tag_columns_ = std::move(columns);
    endResetModel();
}

bool TrackTableModel::set_view_state(TrackViewState state)
{
    state = normalized_track_view_state(std::move(state));
    if (state == state_)
        return false;
    beginResetModel();
    state_ = std::move(state);
    rows_ = visible_rows();
    endResetModel();
    return true;
}

std::vector<TrackRow> TrackTableModel::visible_rows() const
{
    return apply_track_view(source_rows_, repository_, state_.query, state_.sort_keys);
}

std::optional<TrackRowId> TrackTableModel::row_id(const QModelIndex& index) const
{
    if (!index.isValid() || index.row() < 0 ||
        static_cast<std::size_t>(index.row()) >= rows_.size())
        return {};

    return rows_[static_cast<std::size_t>(index.row())].id;
}

} // namespace wiola::gui
