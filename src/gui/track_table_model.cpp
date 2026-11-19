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

#include <QColor>
#include <QFont>

#include <algorithm>
#include <array>

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
    return parent.isValid() ? 0 : Column::num_columns;
}

QVariant TrackTableModel::data(const QModelIndex& index, int role) const
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

    static const std::array labels{QString{}, QString{"Title"}, QString{"Artist"}, QString{"Album"},
        QString{"Length"}};
    return section >= 0 && section < Column::num_columns ? labels[static_cast<std::size_t>(section)]
                                                         : QVariant{};
}

void TrackTableModel::show_tracks(const std::vector<engine::Track>& tracks,
    std::optional<engine::QueueEntryId> current)
{
    const bool same_structure = rows_.size() == tracks.size() &&
        std::ranges::equal(rows_, tracks,
            [](const engine::Track& a, const engine::Track& b) { return a.id() == b.id(); });

    if (!same_structure) {
        beginResetModel();
        rows_ = tracks;
        current_ = current;
        endResetModel();
        return;
    }

    for (std::size_t row = 0; row < rows_.size(); ++row) {
        if (same_metadata(rows_[row], tracks[row]))
            continue;

        rows_[row] = tracks[row];
        const QModelIndex first{index(static_cast<int>(row), 0)};
        const QModelIndex last{index(static_cast<int>(row), Column::num_columns - 1)};
        emit dataChanged(first, last);
    }

    const auto previous{current_};
    current_ = current;

    for (const auto id : {previous, current_}) {
        if (!id)
            continue;

        const auto row = std::ranges::find(rows_, *id, &engine::Track::id);
        if (row != rows_.end()) {
            const auto position = static_cast<int>(std::distance(rows_.begin(), row));
            emit dataChanged(index(position, 0), index(position, Column::num_columns - 1));
        }
    }
}

std::optional<engine::QueueEntryId> TrackTableModel::track_id(const QModelIndex& index) const
{
    if (!index.isValid() || index.row() < 0 ||
        static_cast<std::size_t>(index.row()) >= rows_.size())
        return {};

    return rows_[static_cast<std::size_t>(index.row())].id();
}

} // namespace wiola::gui
