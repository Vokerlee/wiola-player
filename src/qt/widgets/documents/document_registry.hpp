/**
 * @file
 * @brief Weak typed document registration, active identity, and value selections.
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
#include <QObject>
#include <QPointer>
#include <QWidget>
#include <functional>
#include <map>
#include <optional>
#include <vector>
#include <wiola/presentation/documents/document_id.hpp>
#include <wiola/presentation/tracks/track_commands.hpp>
#include <wiola/presentation/tracks/track_identity.hpp>

namespace wiola::gui {

class NowPlayingDocument;
class SavedPlaylistDocument;
class WorkspaceDocument;

class DocumentRegistry final {
public:
    using Selection = std::function<std::vector<SelectedTrack>()>;
    bool add(NowPlayingDocument& document, Selection selection);
    bool add(const library::PlaylistId& id, SavedPlaylistDocument& document, Selection selection);
    bool add(const library::WorkspaceId& id, WorkspaceDocument& document, Selection selection);
    bool remove(const DocumentId& id);
    [[nodiscard]] QWidget* view(const DocumentId& id) const;
    [[nodiscard]] SavedPlaylistDocument* playlist(const library::PlaylistId& id) const;
    [[nodiscard]] WorkspaceDocument* workspace(const library::WorkspaceId& id) const;
    [[nodiscard]] std::optional<DocumentId> identify(const QWidget* view) const;
    bool activate(const DocumentId& id);
    void activate(const QWidget* widget);
    [[nodiscard]] std::optional<DocumentId> active() const;
    [[nodiscard]] std::vector<SelectedTrack> selection() const;
    [[nodiscard]] std::vector<SelectedTrack> selection(const DocumentId& id) const;
    [[nodiscard]] TrackCapabilities capabilities(const DocumentId& id) const;
    [[nodiscard]] std::vector<std::pair<library::PlaylistId, QPointer<SavedPlaylistDocument>>>
    playlists() const;
    [[nodiscard]] std::vector<std::pair<library::WorkspaceId, QPointer<WorkspaceDocument>>>
    workspaces() const;

private:
    struct Entry {
        QPointer<QWidget> view;
        TrackCapabilities capabilities;
        Selection selection;
        std::uint64_t registration;
    };

    bool add(DocumentId id, QWidget& view, TrackCapabilities capabilities, Selection selection);
    std::map<DocumentId, Entry> entries_;
    std::optional<DocumentId> active_;
    std::uint64_t next_{0};
    // Destroy connection contexts before the registry storage and before any owning widget tree.
    QObject lifetime_;
};

} // namespace wiola::gui
