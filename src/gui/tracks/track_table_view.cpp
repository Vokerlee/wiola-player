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
#include <QScrollBar>
#include <QStyledItemDelegate>

#include <algorithm>

namespace wiola::gui {

namespace {

constexpr PresentationTokens tokens;

using Column = StandardTrackColumn;

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
    horizontalHeader()->setSectionsMovable(true);
    for (int section = 0; section < model_.columnCount(); ++section) {
        const auto descriptor = model_.column(section);
        horizontalHeader()->setSectionResizeMode(section, descriptor.resize);
        setColumnWidth(section, descriptor.width);
        column_layout_[descriptor.id] = {descriptor.width, section, true};
        column_order_.push_back(descriptor.id);
    }
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
            [this, id = model_.column(column).id, column](bool shown) {
                remember_columns();
                column_layout_[id].visible = shown;
                setColumnHidden(column, !shown);
                if (shown)
                    setColumnWidth(column, column_layout_[id].width);
            });
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

    command_actions_ = {
        {TrackCommand::play,                 play       },
        {TrackCommand::remove,               remove     },
        {TrackCommand::transfer,             add_active },
        {TrackCommand::transfer_destination, add_to     },
        {TrackCommand::move,                 move_to    },
        {TrackCommand::apply_order,          apply_order}
    };

    connect(this, &QTableView::activated, [this](const QModelIndex& index) {
        if (const auto id = model_.row_id(index); id && capabilities_.supports(TrackCommand::play))
            emit row_chosen(*id);
    });
}

void TrackTableView::set_capabilities(TrackCapabilities capabilities)
{
    capabilities_ = capabilities;
    for (const auto& [command, action] : command_actions_) {
        const bool available = capabilities_.supports(command);
        action->setVisible(available);
        action->setEnabled(available);
    }
}

void TrackTableView::show_rows(const std::vector<TrackRow>& tracks,
    std::optional<TrackRowId> current)
{
    const auto current_id = model_.row_id(currentIndex());
    const QPersistentModelIndex anchor = indexAt(QPoint{0, 0});
    const int offset = anchor.isValid() ? visualRect(anchor).top() : 0;
    model_.show_rows(tracks, current);
    if (current_id && !model_.index_of(*current_id).isValid())
        selectionModel()->setCurrentIndex({}, QItemSelectionModel::NoUpdate);
    if (anchor.isValid()) {
        scrollTo(anchor, QAbstractItemView::PositionAtTop);
        verticalScrollBar()->setValue(verticalScrollBar()->value() - offset);
    }
}

void TrackTableView::show_library(const library::LibraryRepository& repository)
{
    const auto current_id = model_.row_id(currentIndex());
    model_.show_library(repository);
    if (current_id && !model_.index_of(*current_id).isValid())
        selectionModel()->setCurrentIndex({}, QItemSelectionModel::NoUpdate);

    std::vector<std::string> available;
    if (tag_catalog_)
        available = tag_catalog_->keys();
    else {
        app::TagKeyCatalog local;
        for (const auto& media : model_.media_ids())
            if (const auto* record = repository.track(media))
                local.upsert(*record);
        available = local.keys();
    }
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

void TrackTableView::remember_columns()
{
    std::vector<TrackColumnId> current;
    for (int visual = 0; visual < model_.columnCount(); ++visual) {
        const int section = horizontalHeader()->logicalIndex(visual);
        const auto id = model_.column(section).id;
        if (!isColumnHidden(section))
            column_layout_[id].width = columnWidth(section);
        current.push_back(id);
        if (std::ranges::find(column_order_, id) == column_order_.end())
            column_order_.push_back(id);
    }
    auto next = current.begin();
    for (auto& id : column_order_)
        if (std::ranges::find(current, id) != current.end())
            id = *next++;
    for (std::size_t i = 0; i < column_order_.size(); ++i)
        column_layout_[column_order_[i]].order = static_cast<int>(i);
}

void TrackTableView::restore_columns()
{
    std::vector<std::pair<int, int>> order;
    for (int section = 0; section < model_.columnCount(); ++section) {
        const auto descriptor = model_.column(section);
        const auto& saved = column_layout_.at(descriptor.id);
        horizontalHeader()->setSectionResizeMode(section, descriptor.resize);
        setColumnWidth(section, saved.width);
        setColumnHidden(section, !saved.visible);
        order.emplace_back(saved.order, section);
    }
    std::ranges::stable_sort(order);
    for (std::size_t visual = 0; visual < order.size(); ++visual)
        horizontalHeader()->moveSection(horizontalHeader()->visualIndex(order[visual].second),
            static_cast<int>(visual));
    update_sort_indicator();
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

        const auto id = tag_track_column(key).id;
        if (column_layout_
                .try_emplace(id,
                    TrackColumnLayout{120, static_cast<int>(column_layout_.size()),
                        visible->isChecked()})
                .second)
            column_order_.push_back(id);
        connect(visible, &QAction::toggled, this, [this, id](bool shown) {
            remember_columns();
            column_layout_[id].visible = shown;
            update_tag_columns();
        });
    }
}

void TrackTableView::update_tag_columns()
{
    visible_tags_.clear();
    for (const auto& key : available_tags_)
        if (column_layout_.at(tag_track_column(key).id).visible)
            visible_tags_.push_back(key);

    const std::vector<TrackRowId> selected = chosen_rows();
    const auto current_id = model_.row_id(currentIndex());
    model_.show_tag_keys(visible_tags_);
    restore_selection(selected, current_id);

    restore_columns();
}

bool TrackTableView::set_view_state(TrackViewState state)
{
    const auto current = model_.row_id(currentIndex());
    if (!model_.set_view_state(std::move(state)))
        return false;
    if (current && !model_.index_of(*current).isValid())
        selectionModel()->setCurrentIndex({}, QItemSelectionModel::NoUpdate);
    update_sort_indicator();
    emit view_state_changed();
    return true;
}

void TrackTableView::sort_column(int column, bool extend)
{
    auto state = view_state();
    auto& keys = state.sort_keys;
    if (column < 0 || column >= model_.columnCount())
        return;
    const auto descriptor = model_.column(column);
    if (!descriptor.sort)
        return;
    TrackSortKey requested = *descriptor.sort;

    const auto same = [&requested](const TrackSortKey& key) {
        return key.field == requested.field && key.tag_key == requested.tag_key;
    };

    if (!extend) {
        if (keys.size() == 1 && same(keys.front())) {
            requested.direction = keys.front().direction == TrackSortDirection::ascending
                ? TrackSortDirection::descending
                : TrackSortDirection::ascending;
        }

        keys = {requested};
        set_view_state(std::move(state));
        return;
    }

    const auto existing = std::ranges::find_if(keys, same);
    if (existing == keys.end()) {
        keys.push_back(requested);
    } else {
        existing->direction = existing->direction == TrackSortDirection::ascending
            ? TrackSortDirection::descending
            : TrackSortDirection::ascending;
    }

    set_view_state(std::move(state));
}

void TrackTableView::update_sort_indicator()
{
    if (view_state().sort_keys.empty()) {
        horizontalHeader()->setSortIndicatorShown(false);
        return;
    }

    const TrackSortKey& primary = view_state().sort_keys.front();
    int column = -1;

    for (int section = 0; section < model_.columnCount(); ++section) {
        const auto descriptor = model_.column(section);
        if (descriptor.sort && descriptor.sort->field == primary.field &&
            descriptor.sort->tag_key == primary.tag_key) {
            column = section;
            break;
        }
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
    // Used for schema changes only. Ordinary row updates preserve Qt persistent selections.
    selectionModel()->clearSelection();
    for (const auto& id : selected) {
        const auto first = model_.index_of(id);
        if (first.isValid())
            selectionModel()->select(first,
                QItemSelectionModel::Select | QItemSelectionModel::Rows);
    }
    selectionModel()->setCurrentIndex(current ? model_.index_of(*current) : QModelIndex{},
        QItemSelectionModel::NoUpdate);
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
    if (event->key() == Qt::Key_Delete && capabilities_.supports(TrackCommand::remove)) {
        emit removal_asked();
        return;
    }

    QTableView::keyPressEvent(event);
}

} // namespace wiola::gui
