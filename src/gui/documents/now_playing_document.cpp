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
#include <gui/model/documents/queue_rows.hpp>

#include <gui/tracks/track_browser.hpp>

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

    browser_ = new TrackBrowser{TrackCapabilities::queue(), this};
    browser_->table().setAccessibleName("Now Playing tracks");

    QVBoxLayout* layout = new QVBoxLayout{this};
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(heading);
    layout->addWidget(browser_);

    connect(browser_, &TrackBrowser::row_activated, this, [this](TrackRowId row) {
        if (const auto* id = std::get_if<engine::QueueEntryId>(&row))
            emit track_chosen(*id);
    });
    connect(browser_, &TrackBrowser::command_requested, this, [this](TrackCommand command) {
        switch (command) {
        case TrackCommand::remove:
            emit removal_asked();
            break;
        case TrackCommand::transfer:
            emit transfer_active_asked();
            break;
        case TrackCommand::transfer_destination:
            emit transfer_destination_asked();
            break;
        case TrackCommand::apply_order:
            emit order_application_asked();
            break;
        default:
            break;
        }
    });
}

void NowPlayingDocument::present_changes(const QueueRowChanges& changes)
{
    if (changes.structure) {
        browser_->show_rows(source_.rows(), source_.current(), library_);
        browser_->table().finish_browser_restore();
    } else {
        if (!changes.rows.empty())
            browser_->update_rows(changes.rows, library_);
        if (changes.current)
            browser_->table().set_playing_row(source_.current());
    }
}

bool NowPlayingDocument::show_playlist(const engine::QueueView& playlist)
{
    const auto changes = source_.reconcile(playlist, library_);
    present_changes(changes);
    return changes.changed();
}

void NowPlayingDocument::show_library(const library::LibraryRepository& repository)
{
    library_ = &repository;
    source_.invalidate();
    browser_->bind_library(repository);
}

bool NowPlayingDocument::show_library_changes(const engine::QueueView& playlist,
    const library::LibraryRepository& repository, const library::LibraryChangeBatch& batch)
{
    library_ = &repository;
    const auto changes = source_.apply_library(playlist, repository, batch);
    present_changes(changes);
    return changes.playback_changed;
}

std::vector<std::filesystem::path>
NowPlayingDocument::displayed_paths(const engine::QueueView& playlist) const
{
    std::vector<std::filesystem::path> paths;

    for (const engine::QueueEntryId id : queue_entry_ids(browser_->table().displayed_rows())) {
        const auto row = playlist.index_of(id);
        if (row)
            paths.push_back(playlist.tracks()[*row].path());
    }

    return paths;
}

std::vector<engine::QueueEntryId> NowPlayingDocument::chosen_tracks() const
{
    return queue_entry_ids(browser_->table().chosen_rows());
}

} // namespace wiola::gui
