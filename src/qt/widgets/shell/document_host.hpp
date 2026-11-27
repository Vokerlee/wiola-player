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

#include "document_command_context.hpp"

#include <qt/application/library/library_session.hpp>
#include <qt/application/session/session_preferences.hpp>
#include <qt/widgets/documents/document_registry.hpp>
#include <wiola/app/playback/playback_service.hpp>

#include <QTabWidget>
#include <QWidget>

namespace wiola::gui {

class TrackBrowser;
class ViewRegistry;

struct DocumentCommands {
    std::function<void()> selection_changed;
    std::function<void()> edit_track;
    std::function<void(TrackCommand, DocumentCommandContext)> execute;
    std::function<void(library::PlaylistId, std::optional<library::PlaylistEntryId>)> play_playlist;
    std::function<void(library::WorkspaceId, bool)> rescan;
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
    void refresh_playback();
    /// Register the currently owned documents and their layout-visible browser parts.
    void register_views(ViewRegistry& views) const;
    [[nodiscard]] std::optional<library::PlaylistId> active_playlist() const;

    void dispatch(TrackCommand command, const DocumentId& id);
    [[nodiscard]] DocumentCommandContext capture(const DocumentId& id) const;

    void capture_preferences() { save(); }

private:
    void restore_browser(const DocumentId& id, TrackBrowser& browser, bool complete);
    void configure_browser(TrackBrowser& browser);
    [[nodiscard]] std::optional<DocumentId> document_containing(const QWidget& widget) const;
    [[nodiscard]] DocumentCommandContext selection_context(const DocumentId& id) const;
    void refresh(const library::LibraryChangeBatch& batch);
    void restore();
    void save();
    app::LibrarySession& library_;
    app::PlaybackService& playback_;
    DocumentCommands commands_;
    app::SessionPreferences& preferences_;
    DocumentRegistry registry_;
    NowPlayingDocument* queue_;
    bool restoring_{true};
    bool updating_activity_{};
    QIcon activity_icon_;
    qint64 activity_palette_{};
    std::optional<library::PlaylistId> last_playlist_;
    std::uint64_t catalog_revision_{};
    library::LibraryChanges::Subscription changes_;
    QObject lifetime_;
};

} // namespace wiola::gui
