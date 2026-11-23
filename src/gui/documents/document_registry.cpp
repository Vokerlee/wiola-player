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

#include "document_registry.hpp"
#include "now_playing_document.hpp"
#include "saved_playlist_document.hpp"
#include "workspace_document.hpp"

namespace wiola::gui {

bool DocumentRegistry::add(NowPlayingDocument& document, Selection selection)
{
    return add(DocumentId::queue(), document, TrackCapabilities::queue(), std::move(selection));
}

bool DocumentRegistry::add(const library::PlaylistId& id, SavedPlaylistDocument& document,
    Selection selection)
{
    return add(DocumentId::playlist(id), document, TrackCapabilities::playlist(),
        std::move(selection));
}

bool DocumentRegistry::add(const library::WorkspaceId& id, WorkspaceDocument& document,
    Selection selection)
{
    return add(DocumentId::workspace(id), document, TrackCapabilities::workspace(),
        std::move(selection));
}

bool DocumentRegistry::add(DocumentId id, QWidget& widget, TrackCapabilities capabilities,
    Selection selection)
{
    if (!id.valid() || view(id) || !selection)
        return false;
    const auto registration = ++next_;
    entries_.insert_or_assign(id, Entry{&widget, capabilities, std::move(selection), registration});
    QObject::connect(&widget, &QObject::destroyed, &lifetime_, [this, id, registration] {
        const auto found = entries_.find(id);
        if (found != entries_.end() && found->second.registration == registration)
            remove(id);
    });
    return true;
}

bool DocumentRegistry::remove(const DocumentId& id)
{
    if (!entries_.erase(id))
        return false;
    if (active_ == id) {
        active_.reset();
        if (view(DocumentId::queue()))
            active_ = DocumentId::queue();
    }
    return true;
}

QWidget* DocumentRegistry::view(const DocumentId& id) const
{
    const auto found = entries_.find(id);
    return found == entries_.end() ? nullptr : found->second.view.data();
}

SavedPlaylistDocument* DocumentRegistry::playlist(const library::PlaylistId& id) const
{
    return static_cast<SavedPlaylistDocument*>(view(DocumentId::playlist(id)));
}

WorkspaceDocument* DocumentRegistry::workspace(const library::WorkspaceId& id) const
{
    return static_cast<WorkspaceDocument*>(view(DocumentId::workspace(id)));
}

std::optional<DocumentId> DocumentRegistry::identify(const QWidget* widget) const
{
    if (widget)
        for (const auto& [id, entry] : entries_)
            if (entry.view == widget)
                return id;
    return {};
}

bool DocumentRegistry::activate(const DocumentId& id)
{
    if (!view(id))
        return false;
    active_ = id;
    return true;
}

void DocumentRegistry::activate(const QWidget* widget)
{
    active_ = identify(widget);
}

std::optional<DocumentId> DocumentRegistry::active() const
{
    return active_ && view(*active_) ? active_ : std::nullopt;
}

std::vector<SelectedTrack> DocumentRegistry::selection() const
{
    const auto id = active();
    if (!id)
        return {};
    return selection(*id);
}

std::vector<SelectedTrack> DocumentRegistry::selection(const DocumentId& id) const
{
    const auto found = entries_.find(id);
    if (found == entries_.end() || !found->second.view)
        return {};
    const auto capture = found->second.selection;
    return capture();
}

TrackCapabilities DocumentRegistry::capabilities(const DocumentId& id) const
{
    const auto found = entries_.find(id);
    return found == entries_.end() || !found->second.view ? TrackCapabilities{}
                                                          : found->second.capabilities;
}

std::vector<std::pair<library::PlaylistId, QPointer<SavedPlaylistDocument>>>
DocumentRegistry::playlists() const
{
    std::vector<std::pair<library::PlaylistId, QPointer<SavedPlaylistDocument>>> result;
    for (const auto& [id, entry] : entries_)
        if (id.playlist() && entry.view)
            result.emplace_back(*id.playlist(),
                static_cast<SavedPlaylistDocument*>(entry.view.data()));
    return result;
}

std::vector<std::pair<library::WorkspaceId, QPointer<WorkspaceDocument>>>
DocumentRegistry::workspaces() const
{
    std::vector<std::pair<library::WorkspaceId, QPointer<WorkspaceDocument>>> result;
    for (const auto& [id, entry] : entries_)
        if (id.workspace() && entry.view)
            result.emplace_back(*id.workspace(),
                static_cast<WorkspaceDocument*>(entry.view.data()));
    return result;
}

} // namespace wiola::gui
