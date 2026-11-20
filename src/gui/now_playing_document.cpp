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

    QVBoxLayout* layout = new QVBoxLayout{this};
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(heading);
    layout->addWidget(table_);

    connect(table_, &TrackTableView::track_chosen, this, &NowPlayingDocument::track_chosen);
    connect(table_, &TrackTableView::removal_asked, this, &NowPlayingDocument::removal_asked);
    connect(table_, &TrackTableView::transfer_active_asked, this,
        &NowPlayingDocument::transfer_active_asked);
    connect(table_, &TrackTableView::transfer_destination_asked, this,
        &NowPlayingDocument::transfer_destination_asked);
}

void NowPlayingDocument::show_playlist(const engine::Playlist& playlist)
{
    const auto current = playlist.current()
        ? std::optional<engine::QueueEntryId>{playlist.current()->id()}
        : std::nullopt;
    table_->show_tracks(playlist.tracks(), current);
}

void NowPlayingDocument::show_library(const library::LibraryRepository& repository)
{
    table_->show_library(repository);
}

std::vector<engine::QueueEntryId> NowPlayingDocument::chosen_tracks() const
{
    return table_->chosen_tracks();
}

} // namespace wiola::gui
