/**
 * @file
 * @brief Saved-playlist and workspace navigation using repository identities.
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

#include <wiola/library/identity.hpp>
#include <wiola/library/repository.hpp>

#include <QTreeWidget>

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace wiola::gui {

struct WorkspaceNavigationEntry {
    library::WorkspaceId id;
    std::string name;
};

class LibraryNavigation final : public QTreeWidget {
public:
    explicit LibraryNavigation(QWidget* parent = nullptr);

    void show_repository(const library::LibraryRepository& repository);
    void show_workspaces(const std::vector<WorkspaceNavigationEntry>& workspaces);
    [[nodiscard]] std::optional<library::PlaylistId> selected_playlist() const;
    bool select_playlist(const library::PlaylistId& id);
    void set_active_playlist(std::optional<library::PlaylistId> id);
    void on_playlist_chosen(std::function<void(library::PlaylistId)> observer);
    void on_workspace_chosen(std::function<void(library::WorkspaceId)> observer);

private:
    void show_activity();
    std::optional<library::PlaylistId> active_;
    QTreeWidgetItem* playlists_{nullptr};
    QTreeWidgetItem* workspaces_{nullptr};
    std::function<void(library::PlaylistId)> playlist_observer_;
    std::function<void(library::WorkspaceId)> workspace_observer_;
};

} // namespace wiola::gui
