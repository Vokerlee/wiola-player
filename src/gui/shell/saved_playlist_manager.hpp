/**
 * @file
 * @brief Saved playlist management controls and persistence-state presentation.
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

#include "library_navigation.hpp"

#include <desktop/library/saved_playlist_commands.hpp>

#include <QLabel>
#include <QPushButton>
#include <QWidget>

namespace wiola::gui {

class SavedPlaylistManager final : public QWidget {
public:
    SavedPlaylistManager(const library::LibraryRepository& repository,
        app::SavedPlaylistCommands& commands, LibraryNavigation& navigation,
        QWidget* parent = nullptr);

    bool create(const std::string& name);
    bool rename_selected(const std::string& name);
    bool duplicate_selected();
    bool remove_selected();
    void on_changed(std::function<void()> observer);

private:
    [[nodiscard]] std::optional<library::PlaylistId> selected() const;
    bool finish(bool succeeded, const library::PlaylistId& selected = {});
    void show_save_failed();

    const library::LibraryRepository& repository_;
    app::SavedPlaylistCommands& commands_;
    LibraryNavigation& navigation_;
    QPushButton* create_button_{nullptr};
    QPushButton* rename_button_{nullptr};
    QPushButton* duplicate_button_{nullptr};
    QPushButton* remove_button_{nullptr};
    QLabel* save_state_{nullptr};
    std::function<void()> changed_observer_;
};

} // namespace wiola::gui
