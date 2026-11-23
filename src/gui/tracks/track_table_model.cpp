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
#include "track_projection.hpp"
#include <QPointer>
#include <app/library/search_index.hpp>

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

TrackTableModel::TrackTableModel(const QWidget& styled, ArtworkRequest policy)
    : styled_{styled}
    , artwork_request_value_{policy}
{
    projection_timer_.setSingleShot(true);
    projection_timer_.setInterval(25);
    connect(&projection_timer_, &QTimer::timeout, this, &TrackTableModel::request_projection);
}

TrackTableModel::~TrackTableModel()
{
    projection_timer_.stop();
    projector_.reset();
}

std::shared_ptr<const TrackProjectionInput> TrackTableModel::projection_input()
{
    if (snapshot_)
        return snapshot_;
    auto input = std::make_shared<TrackProjectionInput>();
    input->rows = source_rows_;
    for (const auto& row : source_rows_) {
        if (input->search.contains(row.media))
            continue;
        if (search_)
            if (const auto* fields = search_->fields(row.media)) {
                input->search.emplace(row.media, *fields);
                continue;
            }
        if (repository_)
            if (const auto* record = repository_->track(row.media))
                input->search.emplace(row.media, library::search_fields_of(*record));
    }
    ++statistics_.snapshots;
    snapshot_ = std::move(input);
    return snapshot_;
}

bool TrackTableModel::refresh_search_revision()
{
    if (!search_ || search_revision_ == search_->revision())
        return false;
    search_revision_ = search_->revision();
    snapshot_.reset();
    return true;
}

void TrackTableModel::reproject(bool coalesce)
{
    static_cast<void>(refresh_search_revision());
    ++statistics_.queries;
    if (source_rows_.size() < asynchronous_projection_threshold ||
        (!track_query_active(state_.query) && state_.sort_keys.empty())) {
        projection_timer_.stop();
        if (projector_)
            projector_->cancel();
        ++projection_generation_;
        projection_pending_ = false;
        reconcile_rows(visible_rows());
        return;
    }
    projection_pending_ = true;
    if (coalesce) {
        if (projector_)
            projector_->cancel();
        ++projection_generation_;
        if (!projection_timer_.isActive())
            projection_timer_.start();
        return;
    }
    request_projection();
}

void TrackTableModel::request_projection()
{
    projection_timer_.stop();
    if (!projector_)
        projector_ = std::make_unique<TrackProjection>(*this, [this](TrackProjectionResult result) {
            if (result.generation != projection_generation_)
                return;
            projection_pending_ = false;
            struct Prepared {
                std::vector<TrackRow> rows;
                bool attached{};
            };
            auto prepared = std::make_shared<Prepared>();
            prepared->rows = std::move(result.rows);
            const auto generation = result.generation;
            std::function<void()> apply = [owner = QPointer<TrackTableModel>{this}, prepared,
                                              generation] {
                if (!owner || prepared->attached || generation != owner->projection_generation_)
                    return;
                prepared->attached = true;
                owner->reconcile_rows(std::move(prepared->rows));
            };
            const auto attachment = attachment_;
            if (attachment)
                attachment(std::move(apply));
            else
                apply();
        });
    projection_pending_ = true;
    projection_generation_ = projector_->request(projection_input(), state_);
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

        const std::string key = library::normalized_tag_text(tag_columns_[column]);
        QStringList values;

        for (const library::TagRecord& tag : track.tags) {
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
        auto reference = track.artwork;
        if (artwork_source_) {
            const auto current = artwork_source_(track.media);
            if (current.known() &&
                (current.availability() != media::ArtworkAvailability::failed || !reference))
                reference = current;
        }
        return artwork_->cover(styled_, track.media, reference, artwork_request_value_,
            [this, id = track.media] {
                if (artwork_request_)
                    artwork_request_(id);
            });
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
    const bool search_changed = refresh_search_revision();
    if (std::ranges::equal(tracks, source_rows_,
            [](const auto& a, const auto& b) { return a.id == b.id; })) {
        std::vector<TrackRow> changed;
        for (std::size_t i = 0; i < tracks.size(); ++i)
            if (tracks[i] != source_rows_[i]) {
                auto row = tracks[i];
                if (repository_)
                    if (const auto* record = repository_->track(row.media))
                        row.tags = record->tags;
                if (row != source_rows_[i])
                    changed.push_back(std::move(row));
            }
        update_rows(changed);
        if (changed.empty() && search_changed && track_query_active(state_.query))
            reproject(true);
        set_current(std::move(current));
        return;
    }
    ++statistics_.source_rebuilds;
    statistics_.source_rows += tracks.size();
    snapshot_.reset();
    std::set<TrackRowId> seen;
    source_rows_.clear();
    source_media_.clear();
    source_index_.clear();
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
        if (valid && seen.insert(row.id).second) {
            source_rows_.push_back(row);
            auto& held = source_rows_.back();
            if (repository_)
                if (const auto* record = repository_->track(held.media))
                    held.tags = record->tags;
            source_media_[held.media].push_back(source_rows_.size() - 1);
            source_index_.emplace(held.id, source_rows_.size() - 1);
        }
    }
    reproject(true);
    set_current(std::move(current));
}

void TrackTableModel::set_current(std::optional<TrackRowId> current)
{
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

void TrackTableModel::update_rows(const std::vector<TrackRow>& changed)
{
    std::vector<std::size_t> updated;
    for (const auto& row : changed) {
        const auto found = source_index_.find(row.id);
        if (found == source_index_.end())
            continue;
        auto& previous = source_rows_[found->second];
        if (previous == row)
            continue;
        if (previous.media != row.media) {
            auto old = source_media_.find(previous.media);
            if (old != source_media_.end()) {
                std::erase(old->second, found->second);
                if (old->second.empty())
                    source_media_.erase(old);
            }
            source_media_[row.media].push_back(found->second);
        }
        previous = row;
        updated.push_back(found->second);
        ++statistics_.updated_rows;
    }
    if (updated.empty())
        return;
    snapshot_.reset();
    if (!track_query_active(state_.query) && state_.sort_keys.empty()) {
        ++statistics_.commits;
        for (const auto position : updated) {
            const auto& row = source_rows_[position];
            const auto visible = row_index_.find(row.id);
            if (visible == row_index_.end())
                continue;
            rows_[static_cast<std::size_t>(visible->second)] = row;
            emit dataChanged(index(visible->second, 0), index(visible->second, columnCount() - 1));
        }
    } else
        reproject(true);
}

void TrackTableModel::refresh_artwork(const library::MediaId& media)
{
    const auto positions = source_media_.find(media);
    if (positions == source_media_.end() || !artwork_source_)
        return;
    for (const auto position : positions->second) {
        const auto visible = index_of(source_rows_[position].id);
        if (visible.isValid())
            emit dataChanged(visible, visible, {Qt::DecorationRole});
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
    ++statistics_.commits;
    std::map<TrackRowId, std::size_t> positions;
    for (std::size_t i = 0; i < next.size(); ++i)
        positions.emplace(next[i].id, i);
    // Group removals into one suffix. Filtering many interleaved rows must not repeatedly
    // move the retained vector and rebuild its entire index for each small removal range.
    std::vector<bool> retained;
    retained.reserve(rows_.size());
    std::size_t kept = 0;
    for (const auto& row : rows_) {
        const bool keep = positions.contains(row.id);
        retained.push_back(keep);
        kept += keep;
    }
    if (kept != rows_.size()) {
        if (kept != 0) {
            emit layoutAboutToBeChanged({}, QAbstractItemModel::VerticalSortHint);
            const auto old = persistentIndexList();
            std::vector<int> mapped(rows_.size());
            int keep = 0, removed = static_cast<int>(kept);
            for (std::size_t i = 0; i < rows_.size(); ++i)
                mapped[i] = retained[i] ? keep++ : removed++;
            QModelIndexList replacement;
            for (const auto& value : old)
                replacement.push_back(index(mapped[static_cast<std::size_t>(value.row())],
                    value.column()));
            std::vector<TrackRow> partitioned;
            partitioned.reserve(rows_.size());
            for (std::size_t i = 0; i < rows_.size(); ++i)
                if (retained[i])
                    partitioned.push_back(std::move(rows_[i]));
            for (std::size_t i = 0; i < rows_.size(); ++i)
                if (!retained[i])
                    partitioned.push_back(std::move(rows_[i]));
            rows_ = std::move(partitioned);
            rebuild_row_index();
            changePersistentIndexList(old, replacement);
            emit layoutChanged({}, QAbstractItemModel::VerticalSortHint);
        }
        beginRemoveRows({}, static_cast<int>(kept), static_cast<int>(rows_.size()) - 1);
        for (std::size_t i = kept; i < rows_.size(); ++i)
            row_index_.erase(rows_[i].id);
        rows_.erase(rows_.begin() + static_cast<std::ptrdiff_t>(kept), rows_.end());
        endRemoveRows();
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
        for (std::size_t i = static_cast<std::size_t>(first); i < rows_.size(); ++i)
            row_index_.emplace(rows_[i].id, static_cast<int>(i));
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
    snapshot_.reset();
    for (auto& row : source_rows_)
        if (const auto* record = repository.track(row.media))
            row.tags = record->tags;
    reproject();
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
    reproject();
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
