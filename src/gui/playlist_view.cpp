/**
 * @file
 * @brief The queue, as a listener sees it.
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

#include "playlist_view.hpp"

#include "presentation_tokens.hpp"

#include <QHeaderView>
#include <QPainter>
#include <QStyledItemDelegate>

#include <algorithm>

namespace wiola::gui {

namespace {

constexpr PresentationTokens tokens;

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
    , model_{*this, tokens.playlist.artwork}
{
    setModel(&model_);
    setMinimumHeight(tokens.playlist.min_height);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setShowGrid(false);
    setWordWrap(false);
    setTextElideMode(Qt::ElideRight);
    setToolTip("Double-click a track to play it; Delete takes it out of the queue");

    verticalHeader()->setVisible(false);
    verticalHeader()->setDefaultSectionSize(tokens.playlist.artwork.cover_size +
        tokens.playlist.row_padding);
    horizontalHeader()->setSectionResizeMode(Column::cover, QHeaderView::Fixed);
    horizontalHeader()->setSectionResizeMode(Column::title, QHeaderView::Stretch);
    horizontalHeader()->setSectionResizeMode(Column::artist, QHeaderView::Interactive);
    horizontalHeader()->setSectionResizeMode(Column::album, QHeaderView::Interactive);
    horizontalHeader()->setSectionResizeMode(Column::length, QHeaderView::Fixed);
    setColumnWidth(Column::cover, tokens.playlist.artwork.cover_size + tokens.playlist.row_padding);
    setColumnWidth(Column::artist, tokens.playlist.artist_column_width);
    setColumnWidth(Column::album, tokens.playlist.album_column_width);
    setColumnWidth(Column::length, tokens.playlist.length_column_width);
    setItemDelegate(new WithoutTheFocusBox{this});

    connect(this, &QTableView::activated, [this](const QModelIndex& index) {
        if (const auto id = model_.track_id(index))
            emit track_chosen(*id);
    });
}

void PlaylistView::show_playlist(const engine::Playlist& playlist)
{
    const auto current = playlist.current()
        ? std::optional<engine::QueueEntryId>{playlist.current()->id()}
        : std::nullopt;
    model_.show_tracks(playlist.tracks(), current);
}

std::vector<engine::QueueEntryId> PlaylistView::chosen_tracks() const
{
    std::vector<engine::QueueEntryId> tracks;
    for (const QModelIndex& index : selectionModel()->selectedRows()) {
        if (const auto id = model_.track_id(index))
            tracks.push_back(*id);
    }

    return tracks;
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
