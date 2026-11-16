/**
 * @file
 * @brief Incremental Qt model over immutable application playlist row values.
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

#include "playlist_model.hpp"

#include "clock.hpp"

#include <QColor>
#include <QFont>

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
    return a.id() == b.id() && a.title() == b.title() && a.artist() == b.artist() &&
        a.album() == b.album() && a.duration() == b.duration() && a.artwork() == b.artwork();
}

} // namespace

PlaylistModel::PlaylistModel(const QWidget& styled, ArtworkCachePolicy policy)
    : artwork_{styled, policy}
    , policy_{policy}
{
}

int PlaylistModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

int PlaylistModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : Column::num_columns;
}

QVariant PlaylistModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 ||
        static_cast<std::size_t>(index.row()) >= rows_.size())
        return {};

    const engine::Track& track{rows_[static_cast<std::size_t>(index.row())]};

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

    if (role == Qt::DecorationRole && index.column() == Column::cover)
        return artwork_.cover_of(track.id().value, track.artwork());

    if (current_ != static_cast<std::size_t>(index.row()))
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

QVariant PlaylistModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};

    static const std::array labels{QString{}, QString{"Title"}, QString{"Artist"}, QString{"Album"},
        QString{"Length"}};
    return section >= 0 && section < Column::num_columns ? labels[static_cast<std::size_t>(section)]
                                                         : QVariant{};
}

void PlaylistModel::show_playlist(const engine::Playlist& playlist)
{
    if (structural_revision_ != playlist.structural_revision()) {
        beginResetModel();
        rows_ = playlist.tracks();
        current_ = playlist.position();
        structural_revision_ = playlist.structural_revision();
        metadata_revision_ = playlist.metadata_revision();
        selection_revision_ = playlist.selection_revision();
        endResetModel();
        return;
    }

    if (metadata_revision_ != playlist.metadata_revision()) {
        for (std::size_t row = 0; row < rows_.size(); ++row) {
            if (same_metadata(rows_[row], playlist.tracks()[row]))
                continue;

            rows_[row] = playlist.tracks()[row];
            const QModelIndex first{index(static_cast<int>(row), 0)};
            const QModelIndex last{index(static_cast<int>(row), Column::num_columns - 1)};
            emit dataChanged(first, last);
        }
        metadata_revision_ = playlist.metadata_revision();
    }

    if (selection_revision_ != playlist.selection_revision()) {
        const auto previous{current_};
        current_ = playlist.position();
        selection_revision_ = playlist.selection_revision();

        for (const auto row : {previous, current_}) {
            if (row && *row < rows_.size())
                emit dataChanged(index(static_cast<int>(*row), 0),
                    index(static_cast<int>(*row), Column::num_columns - 1));
        }
    }
}

} // namespace wiola::gui
