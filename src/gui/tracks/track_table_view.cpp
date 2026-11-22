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

#include <gui/appearance/presentation_tokens.hpp>

#include <library/tags.hpp>

#include <QAction>
#include <QGuiApplication>
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

class ThemedRowDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
        const QModelIndex& index) const override
    {
        QStyledItemDelegate::paint(painter, option, index);

        if (!option.state.testFlag(QStyle::State_HasFocus))
            return;

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        QPen focus{option.palette.color(QPalette::Highlight), 2};
        painter->setPen(focus);
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(option.rect.adjusted(1, 1, -2, -2), 3, 3);
        painter->restore();
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
    setItemDelegate(new ThemedRowDelegate{this});

    horizontalHeader()->setContextMenuPolicy(Qt::ActionsContextMenu);
    horizontalHeader()->setSectionsClickable(true);
    horizontalHeader()->setSortIndicatorShown(true);

    connect(horizontalHeader(), &QHeaderView::sectionClicked, this, [this](int column) {
        sort_column(column,
            QGuiApplication::keyboardModifiers().testAnyFlags(Qt::ControlModifier |
                Qt::ShiftModifier));
    });

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
        if (const auto id = model_.row_id(currentIndex()))
            emit row_chosen(*id);
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

    QAction* move_to = new QAction{"Move to playlist →", this};
    addAction(move_to);
    connect(move_to, &QAction::triggered, this, &TrackTableView::transfer_move_asked);

    QAction* apply_order = new QAction{"Apply displayed order to playback", this};
    addAction(apply_order);
    connect(apply_order, &QAction::triggered, this, &TrackTableView::order_application_asked);

    connect(this, &QTableView::activated, [this](const QModelIndex& index) {
        if (const auto id = model_.row_id(index))
            emit row_chosen(*id);
    });
}

void TrackTableView::show_rows(const std::vector<TrackRow>& tracks,
    std::optional<TrackRowId> current)
{
    const std::vector<TrackRowId> selected = chosen_rows();
    const auto current_id = model_.row_id(currentIndex());
    tracks_ = tracks;
    current_ = current;

    model_.show_rows(tracks, current);
    restore_selection(selected, current_id);
}

void TrackTableView::show_library(const library::LibraryRepository& repository)
{
    const std::vector<TrackRowId> selected = chosen_rows();
    const auto current_id = model_.row_id(currentIndex());
    model_.show_library(repository);
    restore_selection(selected, current_id);

    std::vector<std::string> available = custom_tag_keys(repository);
    for (const std::string& visible : visible_tags_) {
        const std::string normalized = library::normalized_tag_text(visible);
        const bool present = std::ranges::any_of(available, [&](const std::string& key) {
            return library::normalized_tag_text(key) == normalized;
        });

        if (!present)
            available.push_back(visible);
    }

    if (available == available_tags_)
        return;

    available_tags_ = std::move(available);
    rebuild_tag_actions();
}

void TrackTableView::rebuild_tag_actions()
{
    for (QAction* action : tag_actions_) {
        horizontalHeader()->removeAction(action);
        action->deleteLater();
    }

    tag_actions_.clear();

    for (const std::string& key : available_tags_) {
        QAction* visible = new QAction{"Tag: " + QString::fromStdString(key), horizontalHeader()};
        visible->setProperty("tagKey", QString::fromStdString(key));
        visible->setCheckable(true);
        visible->setChecked(std::ranges::any_of(visible_tags_, [&](const std::string& shown) {
            return library::normalized_tag_text(shown) == library::normalized_tag_text(key);
        }));
        horizontalHeader()->addAction(visible);
        tag_actions_.push_back(visible);

        connect(visible, &QAction::toggled, this, [this](bool) { update_tag_columns(); });
    }
}

void TrackTableView::update_tag_columns()
{
    visible_tags_.clear();

    for (const QAction* action : tag_actions_) {
        if (action->isChecked())
            visible_tags_.push_back(action->property("tagKey").toString().toStdString());
    }

    const std::vector<TrackRowId> selected = chosen_rows();
    const auto current_id = model_.row_id(currentIndex());
    model_.show_tag_keys(visible_tags_);
    restore_selection(selected, current_id);

    for (std::size_t column = 0; column < visible_tags_.size(); ++column)
        setColumnHidden(TrackTableModel::standard_columns + static_cast<int>(column), false);
}

void TrackTableView::sort_by(const std::vector<TrackSortKey>& keys)
{
    const std::vector<TrackRowId> selected = chosen_rows();
    const auto current_id = model_.row_id(currentIndex());
    std::vector<TrackSortKey> valid;

    for (const TrackSortKey& key : keys) {
        if (key.valid())
            valid.push_back(key);
    }

    sort_keys_ = std::move(valid);

    model_.sort_by(sort_keys_);
    restore_selection(selected, current_id);
    update_sort_indicator();
}

void TrackTableView::sort_column(int column, bool extend)
{
    TrackSortKey requested;

    if (column >= TrackTableModel::standard_columns) {
        const std::size_t tag =
            static_cast<std::size_t>(column - TrackTableModel::standard_columns);
        if (tag >= visible_tags_.size())
            return;

        requested = TrackSortKey::for_tag(visible_tags_[tag]);
    } else {
        switch (column) {
        case Column::title:
            requested.field = TrackSortField::title;
            break;
        case Column::artist:
            requested.field = TrackSortField::artist;
            break;
        case Column::album:
            requested.field = TrackSortField::album;
            break;
        case Column::length:
            requested.field = TrackSortField::duration;
            break;
        default:
            return;
        }
    }

    const auto same = [&requested](const TrackSortKey& key) {
        return key.field == requested.field && key.tag_key == requested.tag_key;
    };

    if (!extend) {
        if (sort_keys_.size() == 1 && same(sort_keys_.front())) {
            requested.direction = sort_keys_.front().direction == TrackSortDirection::ascending
                ? TrackSortDirection::descending
                : TrackSortDirection::ascending;
        }

        sort_by({requested});
        return;
    }

    const auto existing = std::ranges::find_if(sort_keys_, same);
    if (existing == sort_keys_.end()) {
        sort_keys_.push_back(requested);
    } else {
        existing->direction = existing->direction == TrackSortDirection::ascending
            ? TrackSortDirection::descending
            : TrackSortDirection::ascending;
    }

    sort_by(sort_keys_);
}

void TrackTableView::update_sort_indicator()
{
    if (sort_keys_.empty()) {
        horizontalHeader()->setSortIndicatorShown(false);
        return;
    }

    const TrackSortKey& primary = sort_keys_.front();
    int column = -1;

    switch (primary.field) {
    case TrackSortField::title:
        column = Column::title;
        break;
    case TrackSortField::artist:
        column = Column::artist;
        break;
    case TrackSortField::album:
        column = Column::album;
        break;
    case TrackSortField::duration:
        column = Column::length;
        break;
    case TrackSortField::custom_tag: {
        const auto tag = std::ranges::find(visible_tags_, primary.tag_key);
        if (tag != visible_tags_.end()) {
            column = TrackTableModel::standard_columns +
                static_cast<int>(std::distance(visible_tags_.begin(), tag));
        }
        break;
    }
    case TrackSortField::path:
        break;
    }

    horizontalHeader()->setSortIndicatorShown(column >= 0);
    if (column >= 0) {
        horizontalHeader()->setSortIndicator(column,
            primary.direction == TrackSortDirection::ascending ? Qt::AscendingOrder
                                                               : Qt::DescendingOrder);
    }
}

void TrackTableView::restore_selection(const std::vector<TrackRowId>& selected,
    const std::optional<TrackRowId>& current)
{
    selectionModel()->clearSelection();

    for (int row = 0; row < model_.rowCount(); ++row) {
        const QModelIndex first = model_.index(row, 0);
        const auto id = model_.row_id(first);

        if (id && std::ranges::find(selected, *id) != selected.end())
            selectionModel()->select(first,
                QItemSelectionModel::Select | QItemSelectionModel::Rows);

        if (id && id == current)
            selectionModel()->setCurrentIndex(first, QItemSelectionModel::NoUpdate);
    }
}

void TrackTableView::search_by(const library::SearchQuery& query)
{
    const std::vector<TrackRowId> selected = chosen_rows();
    const auto current_id = model_.row_id(currentIndex());
    query_ = query;
    query_.filters = filters_;
    model_.search_by(query_);
    restore_selection(selected, current_id);
}

void TrackTableView::filter_by(const std::vector<library::TagFilter>& filters)
{
    const std::vector<TrackRowId> selected = chosen_rows();
    const auto current_id = model_.row_id(currentIndex());
    filters_ = filters;
    query_.filters = filters_;
    model_.search_by(query_);
    restore_selection(selected, current_id);
}

std::size_t TrackTableView::visible_count() const
{
    return static_cast<std::size_t>(model_.rowCount());
}

std::size_t TrackTableView::total_count() const
{
    return model_.total_count();
}

std::vector<TrackRowId> TrackTableView::displayed_rows() const
{
    std::vector<TrackRowId> displayed;
    displayed.reserve(static_cast<std::size_t>(model_.rowCount()));

    for (int row = 0; row < model_.rowCount(); ++row) {
        if (const auto id = model_.row_id(model_.index(row, 0)))
            displayed.push_back(*id);
    }

    return displayed;
}

std::vector<TrackRowId> TrackTableView::chosen_rows() const
{
    std::vector<TrackRowId> tracks;

    for (const QModelIndex& index : selectionModel()->selectedRows()) {
        if (const auto id = model_.row_id(index))
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
