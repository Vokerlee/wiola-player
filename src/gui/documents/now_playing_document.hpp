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

#pragma once

#include <engine/queue/playlist.hpp>

#include <QWidget>

#include <vector>

namespace wiola::library {

class LibraryRepository;

}

namespace wiola::gui {

class TrackTableView;
class TrackSearchPanel;
class TagFilterPanel;

class NowPlayingDocument final : public QWidget {
    Q_OBJECT

public:
    explicit NowPlayingDocument(QWidget* parent = nullptr);

    void show_playlist(const engine::Playlist& playlist);
    void show_library(const library::LibraryRepository& repository);
    [[nodiscard]] std::vector<std::filesystem::path>
    displayed_paths(const engine::Playlist& playlist) const;
    [[nodiscard]] std::vector<engine::QueueEntryId> chosen_tracks() const;

signals:
    void track_chosen(engine::QueueEntryId id);
    void removal_asked();
    void transfer_active_asked();
    void transfer_destination_asked();
    void order_application_asked();

private:
    void update_results();

    TrackTableView* table_{nullptr};
    TrackSearchPanel* search_{nullptr};
    TagFilterPanel* filters_{nullptr};
};

} // namespace wiola::gui
