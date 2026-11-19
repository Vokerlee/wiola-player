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

#include "track_table_view.hpp"

#include <engine/queue/playlist.hpp>
#include <library/repository.hpp>

#include <QLabel>
#include <QPushButton>
#include <QWidget>

#include <functional>

namespace wiola::gui {

class SavedPlaylistDocument final : public QWidget {
public:
    explicit SavedPlaylistDocument(QWidget* parent = nullptr);

    bool show_playlist(const library::LibraryRepository& repository, const library::PlaylistId& id);
    void set_active(bool active);
    [[nodiscard]] std::vector<library::PlaylistEntryId> chosen_entries() const;
    void on_entry_chosen(std::function<void(library::PlaylistEntryId)> observer);
    void on_play_requested(std::function<void()> observer);
    void on_active_requested(std::function<void()> observer);
    void on_transfer_active_requested(std::function<void()> observer);
    void on_transfer_destination_requested(std::function<void()> observer);

private:
    void update_heading();

    QLabel* heading_{nullptr};
    QPushButton* play_button_{nullptr};
    QPushButton* active_button_{nullptr};
    TrackTableView* table_{nullptr};
    engine::Playlist rows_;
    std::vector<library::PlaylistEntryRecord> entries_;
    QString name_;
    bool active_destination_{false};
    std::function<void(library::PlaylistEntryId)> entry_observer_;
    std::function<void()> playback_observer_;
    std::function<void()> active_observer_;
    std::function<void()> transfer_active_observer_;
    std::function<void()> transfer_destination_observer_;
};

} // namespace wiola::gui
