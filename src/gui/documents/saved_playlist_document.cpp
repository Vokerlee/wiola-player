/**
 * @file
 * @brief Read-only saved playlist document with persistent entry identity mapping.
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

#include "saved_playlist_document.hpp"

#include <gui/tracks/tag_filter_panel.hpp>
#include <gui/tracks/track_search_panel.hpp>

#include <QLabel>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace wiola::gui {

SavedPlaylistDocument::SavedPlaylistDocument(QWidget* parent)
    : QWidget{parent}
{
    setObjectName("savedPlaylistDocument");

    heading_ = new QLabel{this};
    heading_->setObjectName("savedPlaylistHeading");
    QFont font = heading_->font();
    font.setBold(true);
    heading_->setFont(font);

    active_button_ = new QPushButton{"Use as active playlist", this};
    active_button_->setObjectName("useActivePlaylist");

    play_button_ = new QPushButton{"Play in Now Playing", this};
    play_button_->setObjectName("playSavedPlaylist");

    table_ = new TrackTableView{this};
    table_->setAccessibleName("Saved playlist tracks");
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
    layout->addWidget(heading_);
    layout->addWidget(active_button_);
    layout->addWidget(play_button_);
    layout->addWidget(search_);
    layout->addWidget(filters_);
    layout->addWidget(table_);

    connect(active_button_, &QPushButton::clicked, [this] {
        if (active_observer_)
            active_observer_();
    });

    connect(play_button_, &QPushButton::clicked, [this] {
        if (playback_observer_)
            playback_observer_();
    });

    connect(table_, &TrackTableView::row_chosen, [this](TrackRowId occurrence) {
        if (const auto* id = std::get_if<library::PlaylistEntryId>(&occurrence);
            id && entry_observer_)
            entry_observer_(*id);
    });

    connect(table_, &TrackTableView::transfer_active_asked, [this] {
        if (transfer_active_observer_)
            transfer_active_observer_();
    });

    connect(table_, &TrackTableView::transfer_destination_asked, [this] {
        if (transfer_destination_observer_)
            transfer_destination_observer_();
    });

    connect(table_, &TrackTableView::transfer_move_asked, [this] {
        if (transfer_move_observer_)
            transfer_move_observer_();
    });

    connect(table_, &TrackTableView::order_application_asked, [this] {
        if (apply_order_observer_)
            apply_order_observer_();
    });
}

bool SavedPlaylistDocument::show_playlist(const library::LibraryRepository& repository,
    const library::PlaylistId& id)
{
    const library::PlaylistRecord* playlist = repository.playlist(id);
    if (playlist == nullptr)
        return false;

    name_ = QString::fromStdString(playlist->name);
    update_heading();

    if (!source_.reconcile(repository, id))
        return false;

    table_->show_library(repository);
    table_->show_rows(source_.rows());
    update_results();
    return true;
}

bool SavedPlaylistDocument::show_media_revision(const library::LibraryRepository& repository,
    const library::MediaId& media)
{
    const bool changed = source_.update_media(repository, media);
    table_->show_library(repository);
    if (changed)
        table_->show_rows(source_.rows());
    update_results();
    return changed;
}

std::vector<SelectedTrack> SavedPlaylistDocument::selection() const
{
    std::vector<SelectedTrack> selected;
    for (const auto& entry : chosen_entries())
        if (const auto* row = source_.row(entry))
            selected.push_back({row->media, row->id, row->location});
    return selected;
}

void SavedPlaylistDocument::set_active(bool active)
{
    active_destination_ = active;
    update_heading();
}

void SavedPlaylistDocument::update_heading()
{
    heading_->setText(active_destination_ ? name_ + " — Active destination" : name_);
}

void SavedPlaylistDocument::update_results()
{
    search_->show_results(table_->visible_count(), table_->total_count());
}

std::vector<std::filesystem::path> SavedPlaylistDocument::displayed_paths() const
{
    std::vector<std::filesystem::path> paths;
    for (const auto& occurrence : table_->displayed_rows())
        if (const auto* id = std::get_if<library::PlaylistEntryId>(&occurrence))
            if (const auto* row = source_.row(*id); row && !row->location.empty())
                paths.push_back(row->location);
    return paths;
}

std::vector<library::PlaylistEntryId> SavedPlaylistDocument::chosen_entries() const
{
    std::vector<library::PlaylistEntryId> selected;
    for (const auto& occurrence : table_->chosen_rows())
        if (const auto* id = std::get_if<library::PlaylistEntryId>(&occurrence))
            selected.push_back(*id);
    return selected;
}

void SavedPlaylistDocument::on_entry_chosen(std::function<void(library::PlaylistEntryId)> observer)
{
    entry_observer_ = std::move(observer);
}

void SavedPlaylistDocument::on_play_requested(std::function<void()> observer)
{
    playback_observer_ = std::move(observer);
}

void SavedPlaylistDocument::on_active_requested(std::function<void()> observer)
{
    active_observer_ = std::move(observer);
}

void SavedPlaylistDocument::on_transfer_active_requested(std::function<void()> observer)
{
    transfer_active_observer_ = std::move(observer);
}

void SavedPlaylistDocument::on_transfer_destination_requested(std::function<void()> observer)
{
    transfer_destination_observer_ = std::move(observer);
}

void SavedPlaylistDocument::on_transfer_move_requested(std::function<void()> observer)
{
    transfer_move_observer_ = std::move(observer);
}

void SavedPlaylistDocument::on_apply_order_requested(std::function<void()> observer)
{
    apply_order_observer_ = std::move(observer);
}

} // namespace wiola::gui
