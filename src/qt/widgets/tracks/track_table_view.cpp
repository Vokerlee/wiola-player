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
#include <QMouseEvent>
#include <QScopedValueRollback>
#include <QSignalBlocker>

#include <qt/widgets/appearance/presentation_tokens.hpp>

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
    model_.on_async_projection([this](auto apply) {
        apply_projection(apply);
        emit view_state_changed();
    });
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
        column_actions_[model_.column(column).id] = visible;

        connect(visible, &QAction::toggled, this,
            [this, id = model_.column(column).id, column](bool shown) {
                remember_columns();
                column_layout_[id].visible = shown;
                setColumnHidden(column, !shown);
                if (shown)
                    setColumnWidth(column, column_layout_[id].width);
                emit browser_preferences_changed();
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
    connect(selectionModel(), &QItemSelectionModel::selectionChanged, this, [this] {
        if (!projecting_)
            pending_restore_.reset();
    });
    connect(horizontalHeader(), &QHeaderView::sectionResized, this, [this] {
        if (!projecting_)
            emit browser_preferences_changed();
    });
    connect(horizontalHeader(), &QHeaderView::sectionMoved, this, [this] {
        if (!projecting_)
            emit browser_preferences_changed();
    });
}

app::BrowserViewState TrackTableView::capture_browser_state()
{
    remember_columns();
    app::BrowserViewState state;
    state.view = view_state();
    state.columns = column_layout_;
    if (pending_restore_) {
        state.selected = pending_restore_->selected;
        state.current = pending_restore_->current;
        state.anchor = pending_restore_->anchor;
        state.anchor_offset = pending_restore_->anchor_offset;
    } else {
        state.selected = chosen_rows();
        state.current = model_.row_id(currentIndex());
        const auto anchor = indexAt(QPoint{0, 0});
        state.anchor = model_.row_id(anchor);
        state.anchor_offset = anchor.isValid() ? visualRect(anchor).top() : 0;
    }
    if (state.selected.size() > 10000)
        state.selected.resize(10000);
    return state;
}

void TrackTableView::restore_browser_state(app::BrowserViewState state)
{
    finish_restore_requested_ = false;
    {
        QScopedValueRollback<bool> projection{projecting_, true};
        for (const auto& [id, column] : state.columns)
            column_layout_.insert_or_assign(id, column);
        std::vector<std::pair<int, TrackColumnId>> ordered;
        for (const auto& [id, column] : column_layout_)
            ordered.emplace_back(column.order, id);
        std::ranges::sort(ordered);
        column_order_.clear();
        for (const auto& [order, id] : ordered)
            column_order_.push_back(id);
        for (std::size_t i = 0; i < column_order_.size(); ++i)
            column_layout_[column_order_[i]].order = static_cast<int>(i);
        visible_tags_.clear();
        for (const auto& [id, column] : column_layout_)
            if (const auto* tag = std::get_if<CustomTagColumn>(&id); tag && column.visible) {
                visible_tags_.push_back(tag->key);
                if (!std::ranges::any_of(available_tags_, [&](const auto& key) {
                        return library::normalized_tag_text(key) ==
                            library::normalized_tag_text(tag->key);
                    }))
                    available_tags_.push_back(tag->key);
            }
        rebuild_tag_actions();
        update_tag_columns();
        pending_restore_ = std::move(state);
    }
    apply_pending_restore();
}

void TrackTableView::apply_pending_restore()
{
    if (!pending_restore_)
        return;
    const auto selected_before = chosen_rows();
    const auto current_before = model_.row_id(currentIndex());
    {
        QScopedValueRollback<bool> projection{projecting_, true};
        const QSignalBlocker selection_delivery{selectionModel()};
        restore_selection(pending_restore_->selected, pending_restore_->current);
        if (pending_restore_->anchor)
            if (const auto index = model_.index_of(*pending_restore_->anchor); index.isValid()) {
                scrollTo(index, QAbstractItemView::PositionAtTop);
                if (verticalScrollMode() == QAbstractItemView::ScrollPerPixel)
                    verticalScrollBar()->setValue(verticalScrollBar()->value() -
                        pending_restore_->anchor_offset);
            }
        const auto present = [&](const TrackRowId& id) {
            return model_.index_of(id).isValid();
        };
        if (std::ranges::all_of(pending_restore_->selected, present) &&
            (!pending_restore_->current || present(*pending_restore_->current)) &&
            (!pending_restore_->anchor || present(*pending_restore_->anchor)))
            pending_restore_.reset();
    }
    if (!projecting_ &&
        (selected_before != chosen_rows() || current_before != model_.row_id(currentIndex())))
        emit presentation_restored();
}

void TrackTableView::finish_browser_restore()
{
    if (model_.projection_pending()) {
        finish_restore_requested_ = true;
        return;
    }
    finish_restore_requested_ = false;
    apply_pending_restore();
    if (pending_restore_) {
        pending_restore_.reset();
        emit browser_preferences_changed();
    }
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
    std::optional<TrackRowId> current, const library::LibraryRepository* repository)
{
    if (repository)
        bind_library(*repository);
    {
        QScopedValueRollback<bool> projection{projecting_, true};

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
    apply_pending_restore();
}

void TrackTableView::update_rows(const std::vector<TrackRow>& rows,
    const library::LibraryRepository* repository)
{
    if (repository)
        bind_library(*repository);
    apply_projection([&] { model_.update_rows(rows); });
    emit view_state_changed();
}

void TrackTableView::show_library(const library::LibraryRepository& repository)
{
    catalog_repository_ = &repository;
    {
        QScopedValueRollback<bool> projection{projecting_, true};
        const auto project = [&] {
            const auto current_id = model_.row_id(currentIndex());
            model_.show_library(repository);
            if (current_id && !model_.index_of(*current_id).isValid())
                selectionModel()->setCurrentIndex({}, QItemSelectionModel::NoUpdate);

            refresh_catalog();
        };
        project();
    }
    apply_pending_restore();
}

void TrackTableView::bind_library(const library::LibraryRepository& repository)
{
    catalog_repository_ = &repository;
    model_.bind_library(repository);
    refresh_catalog();
}

void TrackTableView::refresh_catalog()
{
    if (tag_catalog_) {
        if (catalog_revision_ == tag_catalog_->revision())
            return;
        catalog_revision_ = tag_catalog_->revision();
    }
    std::vector<std::string> available;
    if (tag_catalog_)
        available = tag_catalog_->keys();
    else if (catalog_repository_) {
        app::TagKeyCatalog local;
        for (const auto& media : model_.media_ids())
            if (const auto* record = catalog_repository_->track(media))
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

void TrackTableView::apply_projection(const std::function<void()>& apply)
{
    const auto selected = chosen_rows();
    const auto current = model_.row_id(currentIndex());
    {
        QScopedValueRollback<bool> projection{projecting_, true};
        const QSignalBlocker selection_delivery{selectionModel()};
        const QPersistentModelIndex anchor = indexAt(QPoint{0, 0});
        const int offset = anchor.isValid() ? visualRect(anchor).top() : 0;
        apply();
        if (current && !model_.index_of(*current).isValid())
            selectionModel()->setCurrentIndex({}, QItemSelectionModel::NoUpdate);
        if (anchor.isValid()) {
            scrollTo(anchor, QAbstractItemView::PositionAtTop);
            verticalScrollBar()->setValue(verticalScrollBar()->value() - offset);
        }
        apply_pending_restore();
        if (finish_restore_requested_)
            finish_browser_restore();
    }
    if (selected != chosen_rows() || current != model_.row_id(currentIndex()))
        emit presentation_restored();
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
        if (const auto action = column_actions_.find(descriptor.id);
            action != column_actions_.end()) {
            const QSignalBlocker blocked{action->second};
            action->second->setChecked(saved.visible);
        }
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
        std::erase_if(column_actions_,
            [action](const auto& item) { return item.second == action; });
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
        column_actions_[id] = visible;
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
            emit browser_preferences_changed();
        });
    }
}

void TrackTableView::update_tag_columns()
{
    QScopedValueRollback<bool> projection{projecting_, true};
    const QSignalBlocker selection_signals{selectionModel()};
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
    pending_restore_.reset();
    QScopedValueRollback<bool> projection{projecting_, true};
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

void TrackTableView::mousePressEvent(QMouseEvent* event)
{
    pending_restore_.reset();
    QTableView::mousePressEvent(event);
}

void TrackTableView::keyPressEvent(QKeyEvent* event)
{
    pending_restore_.reset();
    if (event->key() == Qt::Key_Delete && capabilities_.supports(TrackCommand::remove)) {
        emit removal_asked();
        return;
    }

    QTableView::keyPressEvent(event);
}

} // namespace wiola::gui
