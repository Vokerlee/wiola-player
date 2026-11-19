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

#include "clock.hpp"
#include "track_sorter.hpp"

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

enum Column {
    cover,
    title,
    artist,
    album,
    length,
    num_columns,
};

bool same_metadata(const engine::Track& a, const engine::Track& b)
{
    return a.title() == b.title() && a.artist() == b.artist() && a.album() == b.album() &&
        a.duration() == b.duration() && a.artwork() == b.artwork();
}

bool restricted(const library::SearchQuery& query)
{
    return !query.title.empty() || !query.artist.empty() || !query.album.empty() ||
        !query.tags.empty() || !query.filters.empty();
}

bool matches(const engine::Track& track, const library::LibraryRepository* repository,
    const library::SearchQuery& query)
{
    if (!restricted(query))
        return true;

    const library::MediaId media = library::media_id_for(track.path());
    const library::TrackRecord* record = repository != nullptr ? repository->track(media) : nullptr;

    if (record != nullptr)
        return library::matches_search(query, *record);

    library::TrackRecord presented;
    presented.id = media;
    presented.embedded = track.metadata();
    return library::matches_search(query, presented);
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

    const engine::Track& track{rows_[static_cast<std::size_t>(index.row())]};

    if (role == Qt::DisplayRole && index.column() >= standard_columns) {
        const std::size_t column = static_cast<std::size_t>(index.column() - standard_columns);
        if (repository_ == nullptr || column >= tag_columns_.size())
            return {};

        const library::TrackRecord* record =
            repository_->track(library::media_id_for(track.path()));
        if (record == nullptr)
            return {};

        const std::string key = library::normalized_tag_text(tag_columns_[column]);
        QStringList values;

        for (const library::TagRecord& tag : record->tags) {
            if (library::normalized_tag_text(tag.key) == key)
                values.push_back(QString::fromStdString(tag.value));
        }

        return values.join(", ");
    }

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case Column::title:
            return QString::fromStdString(track.title());
        case Column::artist:
            return QString::fromStdString(track.artist());
        case Column::album:
            return QString::fromStdString(track.album());
        case Column::length:
            return track.duration() ? as_clock(*track.duration()) : QString{};
        default:
            return {};
        }
    }

    if (role == Qt::DecorationRole && index.column() == Column::cover) {
        return artwork_.cover_of({.media = library::media_id_for(track.path()).value,
                                     .revision = track.revision()},
            track.artwork());
    }

    if (current_ != track.id())
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

    if (section >= standard_columns) {
        const std::size_t column = static_cast<std::size_t>(section - standard_columns);
        return column < tag_columns_.size() ? QString::fromStdString(tag_columns_[column])
                                            : QVariant{};
    }

    static const std::array labels{QString{}, QString{"Title"}, QString{"Artist"}, QString{"Album"},
        QString{"Length"}};
    return section >= 0 && section < Column::num_columns ? labels[static_cast<std::size_t>(section)]
                                                         : QVariant{};
}

void TrackTableModel::show_tracks(const std::vector<engine::Track>& tracks,
    std::optional<engine::QueueEntryId> current)
{
    const bool same_structure = source_rows_.size() == tracks.size() &&
        std::ranges::equal(source_rows_, tracks,
            [](const engine::Track& a, const engine::Track& b) { return a.id() == b.id(); });
    const bool same_contents = same_structure &&
        std::ranges::equal(source_rows_, tracks,
            [](const engine::Track& a, const engine::Track& b) { return same_metadata(a, b); });

    if (!sort_keys_.empty() || restricted(query_)) {
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

    for (const auto id : {previous, current_}) {
        if (!id)
            continue;

        const auto row = std::ranges::find(rows_, *id, &engine::Track::id);
        if (row != rows_.end()) {
            const auto position = static_cast<int>(std::distance(rows_.begin(), row));
            emit dataChanged(index(position, 0), index(position, columnCount({}) - 1));
        }
    }
}

void TrackTableModel::show_library(const library::LibraryRepository& repository)
{
    repository_ = &repository;
    std::vector<engine::Track> visible = visible_rows();
    const bool different_order = rows_.size() != visible.size() ||
        !std::ranges::equal(rows_, visible,
            [](const engine::Track& a, const engine::Track& b) { return a.id() == b.id(); });

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

void TrackTableModel::sort_by(const std::vector<TrackSortKey>& keys)
{
    std::vector<TrackSortKey> valid;

    for (const TrackSortKey& key : keys) {
        if (key.valid())
            valid.push_back(key);
    }

    if (valid == sort_keys_)
        return;

    beginResetModel();
    sort_keys_ = std::move(valid);
    rows_ = visible_rows();
    endResetModel();
}

void TrackTableModel::search_by(const library::SearchQuery& query)
{
    const library::SearchQuery normalized = library::normalized_search_query(query);
    const bool same = query_.title == normalized.title && query_.artist == normalized.artist &&
        query_.album == normalized.album && query_.tags == normalized.tags &&
        query_.filters == normalized.filters;

    if (same)
        return;

    beginResetModel();
    query_ = normalized;
    rows_ = visible_rows();
    endResetModel();
}

std::vector<engine::Track> TrackTableModel::visible_rows() const
{
    std::vector<engine::Track> visible;
    visible.reserve(source_rows_.size());

    for (const engine::Track& track : source_rows_) {
        if (matches(track, repository_, query_))
            visible.push_back(track);
    }

    if (!sort_keys_.empty() && repository_ != nullptr)
        return sorted_tracks(visible, *repository_, sort_keys_);

    return visible;
}

std::optional<engine::QueueEntryId> TrackTableModel::track_id(const QModelIndex& index) const
{
    if (!index.isValid() || index.row() < 0 ||
        static_cast<std::size_t>(index.row()) >= rows_.size())
        return {};

    return rows_[static_cast<std::size_t>(index.row())].id();
}

std::vector<std::string> custom_tag_keys(const library::LibraryRepository& repository)
{
    std::map<std::string, std::string> keys;

    for (const library::TrackRecord& track : repository.tracks()) {
        for (const library::TagRecord& tag : track.tags)
            keys.try_emplace(library::normalized_tag_text(tag.key), tag.key);
    }

    std::vector<std::string> result;
    result.reserve(keys.size());

    for (const auto& [normalized, key] : keys)
        result.push_back(key);

    return result;
}

} // namespace wiola::gui
