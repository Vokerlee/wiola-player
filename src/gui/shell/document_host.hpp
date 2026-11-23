/**
 * @file
 * @brief Document tabs, typed registration, restoration, and view lifetime.
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
#include <QTabWidget>
#include <QWidget>
#include <app/playback/playback_service.hpp>
#include <desktop/library/library_session.hpp>
#include <desktop/session/session_preferences.hpp>
#include <gui/documents/document_registry.hpp>

namespace wiola::gui {

class TrackBrowser;

struct DocumentCommands {
    std::function<void()> open_folder;
    std::function<void()> selection_changed;
    std::function<void(library::PlaylistId)> activate_playlist;
    std::function<void(library::PlaylistId)> play_playlist;
    std::function<void(library::PlaylistId, SavedPlaylistDocument&, bool)> transfer_playlist;
    std::function<void(library::PlaylistId, SavedPlaylistDocument&)> move_playlist;
    std::function<void(library::WorkspaceId, bool)> rescan;
    std::function<void(WorkspaceDocument&)> play_workspace;
    std::function<void(WorkspaceDocument&)> append_workspace;
    std::function<void(WorkspaceDocument&, bool)> transfer_workspace;
    std::function<void(std::vector<std::filesystem::path>)> apply_order;
};

class DocumentHost final : public QTabWidget {
public:
    DocumentHost(app::LibrarySession& library, app::PlaybackService& playback,
        app::SessionPreferences& preferences, DocumentCommands commands, QWidget* parent = nullptr);
    ~DocumentHost() override;

    [[nodiscard]] NowPlayingDocument& queue() noexcept { return *queue_; }

    [[nodiscard]] QTabWidget& tabs() noexcept { return *this; }

    [[nodiscard]] DocumentRegistry& registry() noexcept { return registry_; }

    bool open_playlist(const library::PlaylistId& id);
    bool open_workspace(const library::WorkspaceId& id);
    bool activate(const DocumentId& id);
    bool close(const DocumentId& id);
    void begin_scan(const library::WorkspaceId& id);
    void refresh_active_destination();

    void capture_preferences() { save(); }

private:
    void restore_browser(const DocumentId& id, TrackBrowser& browser, bool complete);
    void configure_browser(TrackBrowser& browser);
    void refresh(const library::LibraryChangeBatch& batch);
    void restore();
    void save();
    app::LibrarySession& library_;
    app::PlaybackService& playback_;
    DocumentCommands commands_;
    app::SessionPreferences& preferences_;
    DocumentRegistry registry_;
    QWidget* empty_;
    NowPlayingDocument* queue_;
    bool restoring_{true};
    library::LibraryChanges::Subscription changes_;
    QObject lifetime_;
};

} // namespace wiola::gui
