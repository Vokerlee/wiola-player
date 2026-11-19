/**
 * @file
 * @brief Reusable track table with stable selection and user-visible column controls.
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

#include "track_table_view.hpp"

#include "presentation_tokens.hpp"

#include <QAction>
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

TrackTableView::TrackTableView(QWidget* parent)
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
    setToolTip("Double-click a track to play it; Delete removes the selected tracks");
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName("Track list");

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

    horizontalHeader()->setContextMenuPolicy(Qt::ActionsContextMenu);

    for (int column = Column::title; column < Column::num_columns; ++column) {
        const QString label = model_.headerData(column, Qt::Horizontal, Qt::DisplayRole).toString();
        QAction* visible = new QAction{label, horizontalHeader()};
        visible->setCheckable(true);
        visible->setChecked(true);
        horizontalHeader()->addAction(visible);

        connect(visible, &QAction::toggled, this,
            [this, column](bool shown) { setColumnHidden(column, !shown); });
    }

    QAction* play = new QAction{"Play selected track", this};
    addAction(play);
    setContextMenuPolicy(Qt::ActionsContextMenu);

    connect(play, &QAction::triggered, this, [this] {
        if (const auto id = model_.track_id(currentIndex()))
            emit track_chosen(*id);
    });

    QAction* remove = new QAction{"Remove selected tracks", this};
    addAction(remove);
    connect(remove, &QAction::triggered, this, &TrackTableView::removal_asked);

    QAction* add_active = new QAction{"Add to active playlist", this};
    addAction(add_active);
    connect(add_active, &QAction::triggered, this, &TrackTableView::transfer_active_asked);

    QAction* add_to = new QAction{"Add to playlist →", this};
    addAction(add_to);
    connect(add_to, &QAction::triggered, this, &TrackTableView::transfer_destination_asked);

    connect(this, &QTableView::activated, [this](const QModelIndex& index) {
        if (const auto id = model_.track_id(index))
            emit track_chosen(*id);
    });
}

void TrackTableView::show_tracks(const std::vector<engine::Track>& tracks,
    std::optional<engine::QueueEntryId> current)
{
    const std::vector<engine::QueueEntryId> selected = chosen_tracks();
    const auto current_id = model_.track_id(currentIndex());

    model_.show_tracks(tracks, current);
    selectionModel()->clearSelection();

    for (int row = 0; row < model_.rowCount(); ++row) {
        const QModelIndex first = model_.index(row, 0);
        const auto id = model_.track_id(first);

        if (id && std::ranges::find(selected, *id) != selected.end())
            selectionModel()->select(first,
                QItemSelectionModel::Select | QItemSelectionModel::Rows);

        if (id && id == current_id)
            selectionModel()->setCurrentIndex(first, QItemSelectionModel::NoUpdate);
    }
}

std::vector<engine::QueueEntryId> TrackTableView::chosen_tracks() const
{
    std::vector<engine::QueueEntryId> tracks;

    for (const QModelIndex& index : selectionModel()->selectedRows()) {
        if (const auto id = model_.track_id(index))
            tracks.push_back(*id);
    }

    return tracks;
}

void TrackTableView::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Delete) {
        emit removal_asked();
        return;
    }

    QTableView::keyPressEvent(event);
}

} // namespace wiola::gui
