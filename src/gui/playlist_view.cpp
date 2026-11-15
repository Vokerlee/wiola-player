/**
 * @file
 * @brief The queue, as a listener sees it.
 * @author Roman Glaz
 * @copyright © 2026, <vokerlee@gmail.com>
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

#include "playlist_view.hpp"

#include <QHeaderView>
#include <QPainter>
#include <QStyledItemDelegate>

#include <algorithm>

namespace wiola::gui {

namespace {

struct PlaylistViewMetrics {
    ArtworkCachePolicy artwork{};
    int playlist_height{120};
    int row_padding{4};
    int artist_column_width{120};
    int album_column_width{120};
    int length_column_width{60};
};

constexpr PlaylistViewMetrics metrics;

enum Column {
    cover,
    title,
    artist,
    album,
    length,
    num_columns,
};

class WithoutTheFocusBox final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
        const QModelIndex& index) const override
    {
        QStyleOptionViewItem without{option};
        without.state &= ~QStyle::State_HasFocus;
        QStyledItemDelegate::paint(painter, without, index);
    }
};

} // namespace

PlaylistView::PlaylistView(QWidget* parent)
    : QTableView{parent}
    , model_{*this, metrics.artwork}
{
    setModel(&model_);
    setMinimumHeight(metrics.playlist_height);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setShowGrid(false);
    setWordWrap(false);
    setTextElideMode(Qt::ElideRight);
    setToolTip("Double-click a track to play it; Delete takes it out of the queue");

    verticalHeader()->setVisible(false);
    verticalHeader()->setDefaultSectionSize(metrics.artwork.cover_size + metrics.row_padding);
    horizontalHeader()->setSectionResizeMode(Column::cover, QHeaderView::Fixed);
    horizontalHeader()->setSectionResizeMode(Column::title, QHeaderView::Stretch);
    horizontalHeader()->setSectionResizeMode(Column::artist, QHeaderView::Interactive);
    horizontalHeader()->setSectionResizeMode(Column::album, QHeaderView::Interactive);
    horizontalHeader()->setSectionResizeMode(Column::length, QHeaderView::Fixed);
    setColumnWidth(Column::cover, metrics.artwork.cover_size + metrics.row_padding);
    setColumnWidth(Column::artist, metrics.artist_column_width);
    setColumnWidth(Column::album, metrics.album_column_width);
    setColumnWidth(Column::length, metrics.length_column_width);
    setItemDelegate(new WithoutTheFocusBox{this});

    connect(this, &QTableView::activated, [this](const QModelIndex& index) {
        emit track_chosen(static_cast<std::size_t>(index.row()));
    });
}

void PlaylistView::show_playlist(const engine::Playlist& playlist)
{
    model_.show_playlist(playlist);
}

std::vector<std::size_t> PlaylistView::chosen_rows() const
{
    std::vector<std::size_t> rows;
    for (const QModelIndex& index : selectionModel()->selectedRows())
        rows.push_back(static_cast<std::size_t>(index.row()));
    std::ranges::sort(rows, std::ranges::greater{});
    return rows;
}

void PlaylistView::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Delete) {
        emit removal_asked();
        return;
    }

    QTableView::keyPressEvent(event);
}

} // namespace wiola::gui
