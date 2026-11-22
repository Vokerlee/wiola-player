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
#include <set>
#include <type_traits>

namespace wiola::gui {

namespace {

using Column = StandardTrackColumn;

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
    std::set<TrackRowId> seen;
    source_rows_.clear();
    source_rows_.reserve(tracks.size());
    for (const auto& row : tracks) {
        const bool valid = std::visit(
            [](const auto& id) {
                if constexpr (std::is_same_v<std::decay_t<decltype(id)>, WorkspaceTrackId>)
                    return id.workspace.valid() && id.media.valid();
                else
                    return id.valid();
            },
            row.id
        );
        if (valid && seen.insert(row.id).second)
            source_rows_.push_back(row);
    }
    reconcile_rows(visible_rows());
    if (current_ == current)
        return;
    const auto previous = current_;
    current_ = current;
    for (const auto& id : {previous, current_}) {
        if (!id)
            continue;
        const auto first = index_of(*id);
        if (first.isValid())
            emit dataChanged(first, index(first.row(), columnCount() - 1),
                {Qt::FontRole, Qt::BackgroundRole});
    }
}

void TrackTableModel::rebuild_row_index()
{
    row_index_.clear();
    for (std::size_t i = 0; i < rows_.size(); ++i)
        row_index_.emplace(rows_[i].id, static_cast<int>(i));
}

QModelIndex TrackTableModel::index_of(const TrackRowId& id, int column) const
{
    const auto found = row_index_.find(id);
    return found == row_index_.end() ? QModelIndex{} : index(found->second, column);
}

void TrackTableModel::reconcile_rows(std::vector<TrackRow> next)
{
    std::map<TrackRowId, std::size_t> positions;
    for (std::size_t i = 0; i < next.size(); ++i)
        positions.emplace(next[i].id, i);
    // Remove backwards so every emitted range refers to the current model.
    for (int last = static_cast<int>(rows_.size()) - 1; last >= 0;) {
        if (positions.contains(rows_[static_cast<std::size_t>(last)].id)) {
            --last;
            continue;
        }
        int first = last;
        while (first > 0 && !positions.contains(rows_[static_cast<std::size_t>(first - 1)].id))
            --first;
        beginRemoveRows({}, first, last);
        rows_.erase(rows_.begin() + first, rows_.begin() + last + 1);
        rebuild_row_index();
        endRemoveRows();
        last = first - 1;
    }
    std::vector<TrackRow> added;
    for (const auto& row : next)
        if (!row_index_.contains(row.id))
            added.push_back(row);
    if (!added.empty()) {
        const int first = static_cast<int>(rows_.size());
        beginInsertRows({}, first, first + static_cast<int>(added.size()) - 1);
        rows_.insert(rows_.end(), std::make_move_iterator(added.begin()),
            std::make_move_iterator(added.end()));
        rebuild_row_index();
        endInsertRows();
    }
    const bool reordered =
        !std::ranges::equal(rows_, next, [](const auto& a, const auto& b) { return a.id == b.id; });
    if (reordered) {
        emit layoutAboutToBeChanged({}, QAbstractItemModel::VerticalSortHint);
        const auto old = persistentIndexList();
        QModelIndexList replacement;
        for (const auto& value : old)
            replacement.push_back(index(
                static_cast<int>(positions.at(rows_[static_cast<std::size_t>(value.row())].id)),
                value.column()
            ));
        // Keep old values until dataChanged below so reordering and edits remain distinct.
        std::vector<TrackRow> ordered;
        ordered.reserve(next.size());
        for (const auto& row : next)
            ordered.push_back(std::move(rows_[static_cast<std::size_t>(row_index_.at(row.id))]));
        rows_ = std::move(ordered);
        rebuild_row_index();
        changePersistentIndexList(old, replacement);
        emit layoutChanged({}, QAbstractItemModel::VerticalSortHint);
    }
    for (std::size_t i = 0; i < next.size(); ++i) {
        if (rows_[i] == next[i])
            continue;
        rows_[i] = std::move(next[i]);
        emit dataChanged(index(static_cast<int>(i), 0),
            index(static_cast<int>(i), columnCount() - 1));
    }
}

void TrackTableModel::show_library(const library::LibraryRepository& repository)
{
    repository_ = &repository;
    for (auto& row : source_rows_)
        if (const auto* record = repository.track(row.media))
            row.tags = record->tags;
    reconcile_rows(visible_rows());
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

    if (!tag_columns_.empty()) {
        beginRemoveColumns({}, standard_columns, columnCount() - 1);
        tag_columns_.clear();
        endRemoveColumns();
    }
    if (!columns.empty()) {
        beginInsertColumns({}, standard_columns,
            standard_columns + static_cast<int>(columns.size()) - 1);
        tag_columns_ = std::move(columns);
        endInsertColumns();
    }
}

bool TrackTableModel::set_view_state(TrackViewState state)
{
    state = normalized_track_view_state(std::move(state));
    if (state == state_)
        return false;
    state_ = std::move(state);
    reconcile_rows(visible_rows());
    return true;
}

std::vector<TrackRow> TrackTableModel::visible_rows() const
{
    return apply_track_view(source_rows_, repository_, state_.query, state_.sort_keys, search_);
}

std::optional<TrackRowId> TrackTableModel::row_id(const QModelIndex& index) const
{
    if (!index.isValid() || index.row() < 0 ||
        static_cast<std::size_t>(index.row()) >= rows_.size())
        return {};

    return rows_[static_cast<std::size_t>(index.row())].id;
}

} // namespace wiola::gui
