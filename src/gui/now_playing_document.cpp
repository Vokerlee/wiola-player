/**
 * @file
 * @brief Dedicated document presenting the engine's real playback queue.
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

#include "now_playing_document.hpp"

#include "tag_filter_panel.hpp"
#include "track_search_panel.hpp"
#include "track_table_view.hpp"

#include <QLabel>
#include <QVBoxLayout>

namespace wiola::gui {

NowPlayingDocument::NowPlayingDocument(QWidget* parent)
    : QWidget{parent}
{
    setObjectName("nowPlayingDocument");
    setAccessibleName("Now Playing");

    QLabel* heading = new QLabel{"Now Playing", this};
    heading->setObjectName("nowPlayingHeading");

    table_ = new TrackTableView{this};
    table_->setAccessibleName("Now Playing tracks");
    search_ = new TrackSearchPanel{this};
    search_->on_changed([this](library::SearchQuery query) {
        table_->search_by(query);
        update_results();
    });
    filters_ = new TagFilterPanel{this};
    filters_->on_changed([this](std::vector<library::TagFilter> filters) {
        table_->filter_by(filters);
        update_results();
    });

    QVBoxLayout* layout = new QVBoxLayout{this};
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(heading);
    layout->addWidget(search_);
    layout->addWidget(filters_);
    layout->addWidget(table_);

    connect(table_, &TrackTableView::track_chosen, this, &NowPlayingDocument::track_chosen);
    connect(table_, &TrackTableView::removal_asked, this, &NowPlayingDocument::removal_asked);
    connect(table_, &TrackTableView::transfer_active_asked, this,
        &NowPlayingDocument::transfer_active_asked);
    connect(table_, &TrackTableView::transfer_destination_asked, this,
        &NowPlayingDocument::transfer_destination_asked);
    connect(table_, &TrackTableView::order_application_asked, this,
        &NowPlayingDocument::order_application_asked);
}

void NowPlayingDocument::show_playlist(const engine::Playlist& playlist)
{
    const auto current = playlist.current()
        ? std::optional<engine::QueueEntryId>{playlist.current()->id()}
        : std::nullopt;
    table_->show_tracks(playlist.tracks(), current);
    update_results();
}

void NowPlayingDocument::show_library(const library::LibraryRepository& repository)
{
    table_->show_library(repository);
    update_results();
}

void NowPlayingDocument::update_results()
{
    search_->show_results(table_->visible_count(), table_->total_count());
}

std::vector<std::filesystem::path>
NowPlayingDocument::displayed_paths(const engine::Playlist& playlist) const
{
    std::vector<std::filesystem::path> paths;

    for (const engine::QueueEntryId id : table_->displayed_tracks()) {
        const auto row = playlist.index_of(id);
        if (row)
            paths.push_back(playlist.tracks()[*row].path());
    }

    return paths;
}

std::vector<engine::QueueEntryId> NowPlayingDocument::chosen_tracks() const
{
    return table_->chosen_tracks();
}

} // namespace wiola::gui
