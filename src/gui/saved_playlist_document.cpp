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

#include <QLabel>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace wiola::gui {

namespace {

bool same_entries(const std::vector<library::PlaylistEntryRecord>& a,
    const std::vector<library::PlaylistEntryRecord>& b)
{
    return a.size() == b.size() &&
        std::ranges::equal(a, b, [](const auto& first, const auto& second) {
            return first.id == second.id && first.media == second.media;
        });
}

} // namespace

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

    QVBoxLayout* layout = new QVBoxLayout{this};
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(heading_);
    layout->addWidget(active_button_);
    layout->addWidget(play_button_);
    layout->addWidget(table_);

    connect(active_button_, &QPushButton::clicked, [this] {
        if (active_observer_)
            active_observer_();
    });

    connect(play_button_, &QPushButton::clicked, [this] {
        if (playback_observer_)
            playback_observer_();
    });

    connect(table_, &TrackTableView::track_chosen, [this](engine::QueueEntryId id) {
        const auto row = rows_.index_of(id);
        if (row && *row < entries_.size() && entry_observer_)
            entry_observer_(entries_[*row].id);
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

    if (!same_entries(entries_, playlist->entries)) {
        std::vector<engine::Track> tracks;
        tracks.reserve(playlist->entries.size());

        for (const library::PlaylistEntryRecord& entry : playlist->entries) {
            const library::TrackRecord* media = repository.track(entry.media);
            const std::filesystem::path location = media != nullptr && !media->locations.empty()
                ? media->locations.front()
                : std::filesystem::path{};
            tracks.push_back(engine::Track::of(location));
        }

        rows_.set(std::move(tracks));
        entries_ = playlist->entries;
    }

    for (std::size_t row = 0; row < entries_.size(); ++row) {
        const library::TrackRecord* media = repository.track(entries_[row].media);
        if (media == nullptr)
            continue;

        static_cast<void>(rows_.describe(rows_.tracks()[row].id(),
            engine::TrackMetadata{.title = media->display_title(),
                .artist = media->display_artist(),
                .album = media->display_album(),
                .duration = media->embedded.duration}));
    }

    table_->show_tracks(rows_.tracks());
    return true;
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

std::vector<library::PlaylistEntryId> SavedPlaylistDocument::chosen_entries() const
{
    std::vector<library::PlaylistEntryId> selected;

    for (const engine::QueueEntryId id : table_->chosen_tracks()) {
        const auto row = rows_.index_of(id);
        if (row && *row < entries_.size())
            selected.push_back(entries_[*row].id);
    }

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

} // namespace wiola::gui
