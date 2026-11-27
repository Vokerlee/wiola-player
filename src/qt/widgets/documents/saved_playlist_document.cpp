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

#include <qt/widgets/appearance/presentation_tokens.hpp>
#include <qt/widgets/appearance/theme.hpp>
#include <qt/widgets/documents/playlist_name.hpp>
#include <qt/widgets/tracks/track_browser.hpp>

#include <QEvent>
#include <QHBoxLayout>
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
    heading_->setTextFormat(Qt::PlainText);
    heading_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

    QFont font = heading_->font();
    font.setBold(true);
    heading_->setFont(font);

    activity_ = new QLabel{this};
    activity_->setObjectName("playlistActivityIndicator");
    activity_->setAccessibleName("Active playlist");
    activity_->setToolTip("Active playlist: contains the current track");
    activity_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    activity_->hide();

    auto* heading = new QHBoxLayout;
    heading->addWidget(activity_);
    heading->addWidget(heading_, 1);

    browser_ = new TrackBrowser{TrackCapabilities::playlist(), this};
    browser_->table().setAccessibleName("Saved playlist tracks");

    QVBoxLayout* layout = new QVBoxLayout{this};
    layout->setContentsMargins(PresentationTokens{}.player.control_spacing, 0, 0, 0);
    layout->addLayout(heading);
    layout->addWidget(browser_);

    connect(browser_, &TrackBrowser::row_activated, this, [this](TrackRowId occurrence) {
        if (const auto* id = std::get_if<library::PlaylistEntryId>(&occurrence);
            id && entry_observer_)
            entry_observer_(*id);
    });

    connect(browser_, &TrackBrowser::command_requested, this, [this](TrackCommand command) {
        switch (command) {
        case TrackCommand::transfer:
            if (transfer_active_observer_)
                transfer_active_observer_();
            break;
        case TrackCommand::transfer_destination:
            if (transfer_destination_observer_)
                transfer_destination_observer_();
            break;
        case TrackCommand::move:
            if (transfer_move_observer_)
                transfer_move_observer_();
            break;
        case TrackCommand::apply_order:
            if (apply_order_observer_)
                apply_order_observer_();
            break;
        default:
            break;
        }
    });
}

bool SavedPlaylistDocument::show_playlist(const library::LibraryRepository& repository,
    const library::PlaylistId& id)
{
    const library::PlaylistRecord* playlist = repository.playlist(id);

    if (playlist == nullptr)
        return false;

    name_ = playlist_name(*playlist);
    update_heading();

    if (!source_.reconcile(repository, id))
        return false;

    browser_->show_rows(source_.rows(), {}, &repository);

    return true;
}

bool SavedPlaylistDocument::show_media_revision(const library::LibraryRepository& repository,
    const library::MediaId& media)
{
    if (!source_.update_media(repository, media))
        return false;

    browser_->update_rows(source_.media_rows(media), &repository);

    return true;
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
    if (active_ == active)
        return;

    active_ = active;
    update_heading();
}

void SavedPlaylistDocument::update_heading()
{
    heading_->setText(name_);
    heading_->setAccessibleDescription(active_ ? "Active playlist" : "");
    heading_->setToolTip(active_ ? name_ + "\nActive playlist" : name_);

    const int size = style()->pixelMetric(QStyle::PM_SmallIconSize, nullptr, this);
    activity_->setPixmap(playlist_activity_icon(*this).pixmap(QSize{size, size},
        devicePixelRatioF()));
    activity_->setVisible(active_);
}

void SavedPlaylistDocument::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);

    if (activity_ &&
        (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange ||
            event->type() == QEvent::FontChange))
        update_heading();
}

std::vector<std::filesystem::path> SavedPlaylistDocument::displayed_paths() const
{
    std::vector<std::filesystem::path> paths;

    for (const auto& occurrence : browser_->table().displayed_rows())
        if (const auto* id = std::get_if<library::PlaylistEntryId>(&occurrence))
            if (const auto* row = source_.row(*id); row && !row->location.empty())
                paths.push_back(row->location);

    return paths;
}

std::vector<library::PlaylistEntryId> SavedPlaylistDocument::chosen_entries() const
{
    std::vector<library::PlaylistEntryId> selected;

    for (const auto& occurrence : browser_->table().chosen_rows())
        if (const auto* id = std::get_if<library::PlaylistEntryId>(&occurrence))
            selected.push_back(*id);

    return selected;
}

void SavedPlaylistDocument::on_entry_chosen(std::function<void(library::PlaylistEntryId)> observer)
{
    entry_observer_ = std::move(observer);
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
