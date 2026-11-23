/**
 * @file
 * @brief Shared track browsing composition with explicit command capabilities.
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

#include "track_browser.hpp"
#include <QVBoxLayout>
#include <algorithm>

namespace wiola::gui {

TrackBrowser::TrackBrowser(TrackCapabilities capabilities, QWidget* parent)
    : QWidget{parent}
    , table_{new TrackTableView{this}}
    , search_{new TrackSearchPanel{this}}
    , filters_{new TagFilterPanel{this}}
{
    setObjectName("trackBrowser");
    table_->set_capabilities(capabilities);
    search_timer_.setSingleShot(true);
    search_timer_.setInterval(75);
    connect(&search_timer_, &QTimer::timeout, &lifetime_, [this] {
        if (pending_query_) {
            auto query = std::move(*pending_query_);
            pending_query_.reset();
            apply_query(std::move(query));
        }
    });
    search_->on_changed([this](library::SearchQuery query) {
        if (table_->total_count() >= TrackTableModel::asynchronous_projection_threshold) {
            pending_query_ = std::move(query);
            search_timer_.start();
        } else
            apply_query(std::move(query));
    });
    filters_->on_edit([this](TagFilterEdit edit) {
        auto state = table_->view_state();
        if (edit.kind == TagFilterEdit::Kind::add)
            state.query.filters.push_back(std::move(edit.filter));
        else
            std::erase(state.query.filters, edit.filter);
        table_->set_view_state(std::move(state));
    });
    connect(table_.data(), &TrackTableView::view_state_changed, &lifetime_, [this] {
        filters_->show_filters(table_->view_state().query.filters);
        update_results();
    });
    auto* layout = new QVBoxLayout{this};
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(search_);
    layout->addWidget(filters_);
    layout->addWidget(table_);
    connect(table_.data(), &TrackTableView::row_chosen, &lifetime_,
        [this](TrackRowId id) { emit row_activated(std::move(id)); });
    connect(table_.data(), &TrackTableView::removal_asked, &lifetime_,
        [this] { emit command_requested(TrackCommand::remove); });
    connect(table_.data(), &TrackTableView::transfer_active_asked, &lifetime_,
        [this] { emit command_requested(TrackCommand::transfer); });
    connect(table_.data(), &TrackTableView::transfer_destination_asked, &lifetime_,
        [this] { emit command_requested(TrackCommand::transfer_destination); });
    connect(table_.data(), &TrackTableView::transfer_move_asked, &lifetime_,
        [this] { emit command_requested(TrackCommand::move); });
    connect(table_.data(), &TrackTableView::order_application_asked, &lifetime_,
        [this] { emit command_requested(TrackCommand::apply_order); });
    connect(table_.data(), &TrackTableView::presentation_restored, &lifetime_,
        [this] { emit selection_changed(); });
    connect(table_->selectionModel(), &QItemSelectionModel::selectionChanged, &lifetime_,
        [this] { emit selection_changed(); });
    update_results();
}

TrackBrowser::~TrackBrowser()
{
    if (search_)
        search_->on_changed({});
    if (filters_)
        filters_->on_edit({});
}

void TrackBrowser::apply_query(library::SearchQuery query)
{
    auto state = table_->view_state();
    query.filters = state.query.filters;
    state.query = std::move(query);
    table_->set_view_state(std::move(state));
}

void TrackBrowser::set_view_state(TrackViewState state)
{
    search_timer_.stop();
    pending_query_.reset();
    table_->set_view_state(std::move(state));
    search_->show_query(table_->view_state().query);
    filters_->show_filters(table_->view_state().query.filters);
    update_results();
}

void TrackBrowser::show_rows(const std::vector<TrackRow>& rows, std::optional<TrackRowId> current,
    const library::LibraryRepository* repository)
{
    table_->show_rows(rows, std::move(current), repository);
    update_results();
}

void TrackBrowser::show_library(const library::LibraryRepository& repository)
{
    table_->show_library(repository);
    update_results();
}

void TrackBrowser::update_results()
{
    search_->show_results(table_->visible_count(), table_->total_count(),
        table_->projection_pending());
}

} // namespace wiola::gui
