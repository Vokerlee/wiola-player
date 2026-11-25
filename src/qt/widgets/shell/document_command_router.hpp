/**
 * @file
 * @brief A length of time, written the way a listener reads it.
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
#include "player_dialogs.hpp"
#include <QObject>
#include <functional>
#include <qt/application/library/library_documents.hpp>
#include <qt/application/library/library_session.hpp>
#include <qt/application/playback/queue_settings.hpp>

namespace wiola::gui {

struct DocumentCommandOutcome {
    QString message;
    bool queue_changed{};
    std::optional<engine::OpenResult> open_result{};
    std::optional<library::PlaylistId> open_playlist{};
    std::optional<library::WorkspaceId> open_workspace{};
};

/// Dispatches captured document intents through application services; owns no views or menus.
class DocumentCommandRouter final : public QObject {
public:
    using Observer = std::function<void(const DocumentCommandOutcome&)>;
    DocumentCommandRouter(app::LibrarySession& library, app::PlaybackService& playback,
        app::QueueSettings& queue, PlayerDialogs& dialogs, Observer observer,
        QObject* parent = nullptr);
    void execute(TrackCommand command, DocumentCommandContext context);
    void open(std::vector<std::filesystem::path> paths, bool append = false);
    void choose_tracks(bool append = false);
    void choose_folder();
    void import_playlist();
    void export_playlist(std::optional<library::PlaylistId> id);
    void play_saved(const library::PlaylistId& id,
        std::optional<library::PlaylistEntryId> entry = {});
    void rescan(const library::WorkspaceId& id, bool recursive);
    void clear_queue();

private:
    void publish(DocumentCommandOutcome outcome);
    QPointer<app::LibrarySession> library_;
    app::PlaybackService& playback_;
    app::QueueSettings& queue_;
    PlayerDialogs& dialogs_;
    Observer observer_;
};

} // namespace wiola::gui
