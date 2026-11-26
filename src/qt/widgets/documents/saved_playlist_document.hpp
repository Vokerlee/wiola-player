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

#pragma once

#include <wiola/library/repository.hpp>
#include <wiola/presentation/documents/saved_playlist_rows.hpp>

#include <QLabel>
#include <QPushButton>
#include <QWidget>

#include <functional>

namespace wiola::gui {

class TrackBrowser;

class SavedPlaylistDocument final : public QWidget {
public:
    explicit SavedPlaylistDocument(QWidget* parent = nullptr);

    [[nodiscard]] TrackBrowser& browser() noexcept { return *browser_; }

    bool show_playlist(const library::LibraryRepository& repository, const library::PlaylistId& id);
    bool show_media_revision(const library::LibraryRepository& repository,
        const library::MediaId& media);
    [[nodiscard]] std::vector<SelectedTrack> selection() const;
    void set_active(bool active);
    [[nodiscard]] std::vector<std::filesystem::path> displayed_paths() const;
    [[nodiscard]] std::vector<library::PlaylistEntryId> chosen_entries() const;
    void on_apply_order_requested(std::function<void()> observer);
    void on_entry_chosen(std::function<void(library::PlaylistEntryId)> observer);
    void on_play_requested(std::function<void()> observer);
    void on_active_requested(std::function<void()> observer);
    void on_transfer_active_requested(std::function<void()> observer);
    void on_transfer_destination_requested(std::function<void()> observer);
    void on_transfer_move_requested(std::function<void()> observer);

private:
    void update_heading();

    QLabel* heading_{nullptr};
    QPushButton* play_button_{nullptr};
    QPushButton* active_button_{nullptr};
    TrackBrowser* browser_{nullptr};
    SavedPlaylistRows source_;
    QString name_;
    bool active_destination_{false};
    std::function<void(library::PlaylistEntryId)> entry_observer_;
    std::function<void()> playback_observer_;
    std::function<void()> active_observer_;
    std::function<void()> transfer_active_observer_;
    std::function<void()> transfer_destination_observer_;
    std::function<void()> transfer_move_observer_;
    std::function<void()> apply_order_observer_;
};

} // namespace wiola::gui
