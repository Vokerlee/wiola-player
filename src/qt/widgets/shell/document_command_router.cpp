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

#include "document_command_router.hpp"
#include <algorithm>
#include <app/playback/playback_queue.hpp>
#include <app/playback/playlist_transfer.hpp>
#include <qt/paths.hpp>
#include <wiola/library/records.hpp>

namespace wiola::gui {

DocumentCommandRouter::DocumentCommandRouter(app::LibrarySession& library,
    app::PlaybackService& playback, app::QueueSettings& queue, PlayerDialogs& dialogs,
    Observer observer, QObject* parent)
    : QObject{parent}
    , library_{&library}
    , playback_{playback}
    , queue_{queue}
    , dialogs_{dialogs}
    , observer_{std::move(observer)}
{
}

void DocumentCommandRouter::publish(DocumentCommandOutcome outcome)
{
    const auto observer = observer_;
    if (observer)
        observer(outcome);
}

void DocumentCommandRouter::open(std::vector<std::filesystem::path> paths, bool append)
{
    if (!library_ || paths.empty())
        return;
    const auto first = platform::qt::QtPathAdapter::to_qt(paths.front().filename());
    for (const auto& path : paths)
        library_->request_metadata(path);
    const auto result = append
        ? app::PlaybackQueue{}.append_paths(playback_, std::move(paths))
        : app::PlaybackQueue{}.replace_with_paths(playback_, std::move(paths));
    queue_.save();
    publish({append ? QString{"%1 track(s) added to Now Playing"}.arg(result.queued)
                    : "opening " + first + "...",
        true, append ? std::nullopt : std::optional{result.open_result}});
}

void DocumentCommandRouter::choose_tracks(bool append)
{
    if (!library_)
        return;
    const QPointer<DocumentCommandRouter> alive{this};
    const auto paths = dialogs_.tracks(playback_.playback_extensions());
    if (alive && library_)
        open(paths, append);
}

void DocumentCommandRouter::choose_folder()
{
    if (!library_)
        return;
    const QPointer<DocumentCommandRouter> alive{this};
    const auto path = dialogs_.folder();
    if (!alive || !library_ || !path)
        return;
    const auto result =
        app::LibraryDocuments{library_->repository(), library_->persistence()}.open_folder(*path);
    if (result.error != app::LibraryDocumentError::none) {
        publish({"Could not save the folder workspace"});
        return;
    }
    DocumentCommandOutcome outcome;
    outcome.open_workspace = result.id;
    publish(std::move(outcome));
}

void DocumentCommandRouter::import_playlist()
{
    if (!library_)
        return;
    const QPointer<DocumentCommandRouter> alive{this};
    const auto path = dialogs_.file(PlayerFileChoice::import_playlist);
    if (!alive || !library_ || !path)
        return;
    const auto result =
        app::LibraryDocuments{library_->repository(), library_->persistence()}.import_m3u(*path);
    if (result.error != app::LibraryDocumentError::none) {
        publish({"Could not import or save the M3U8 playlist"});
        return;
    }
    DocumentCommandOutcome outcome;
    outcome.message = "M3U8 playlist imported";
    outcome.open_playlist = result.id;
    publish(std::move(outcome));
}

void DocumentCommandRouter::export_playlist(std::optional<library::PlaylistId> id)
{
    if (!library_)
        return;
    if (!id)
        id = library_->active_playlist().id();
    if (!id) {
        publish({"Open a saved playlist to export"});
        return;
    }
    const QPointer<DocumentCommandRouter> alive{this};
    const auto path = dialogs_.file(PlayerFileChoice::export_playlist);
    if (!alive || !library_ || !path)
        return;
    const auto error =
        app::LibraryDocuments{library_->repository(), library_->persistence()}.export_m3u(*id,
            *path);
    publish({error == app::LibraryDocumentError::none ? "M3U8 playlist exported"
                                                      : "Could not export the M3U8 playlist"});
}

void DocumentCommandRouter::play_saved(const library::PlaylistId& id,
    std::optional<library::PlaylistEntryId> entry)
{
    if (!library_)
        return;
    const auto result = app::PlaybackQueue{}.replace_with_saved_playlist(playback_,
        library_->repository(), id, entry);
    if (!result.requested) {
        publish({"The saved playlist has no playable tracks for this command"});
        return;
    }
    queue_.save();
    publish({result.unavailable ? QString{"%1 unavailable track(s) skipped"}.arg(result.unavailable)
                                : QString{"opening saved playlist..."},
        true, result.open_result});
}

void DocumentCommandRouter::clear_queue()
{
    if (!library_)
        return;
    playback_.clear();
    queue_.save();
    publish({{}, true});
}

void DocumentCommandRouter::rescan(const library::WorkspaceId& id, bool recursive)
{
    if (!library_)
        return;
    const auto* current = library_->repository().workspace(id);
    if (!current)
        return;
    const bool changed = current->recursive != recursive;
    const auto error =
        app::LibraryDocuments{library_->repository(), library_->persistence()}.set_recursive(id,
            recursive);
    if (error != app::LibraryDocumentError::none) {
        publish({"Could not save the workspace settings"});
        return;
    }
    if (!changed)
        static_cast<void>(library_->scan_workspace(id, playback_.playback_extensions()));
}

void DocumentCommandRouter::execute(TrackCommand command, DocumentCommandContext context)
{
    if (!library_ || !context.document.valid() || !context.capabilities.supports(command))
        return;
    if (command == TrackCommand::activate_destination) {
        if (const auto* id = context.document.playlist();
            id && library_->repository().playlist(*id))
            static_cast<void>(library_->active_playlist().set(*id));
        return;
    }
    if (command == TrackCommand::remove) {
        if (!context.document.is_queue())
            return;
        for (const auto& selected : context.selection)
            if (const auto* id = std::get_if<engine::QueueEntryId>(&selected.occurrence))
                if (const auto index = playback_.playlist().index_of(*id))
                    static_cast<void>(playback_.remove(*index));
        queue_.save();
        publish({{}, true});
        return;
    }
    if (command == TrackCommand::play && context.document.is_queue()) {
        if (context.selection.empty())
            return;
        if (const auto* id =
                std::get_if<engine::QueueEntryId>(&context.selection.front().occurrence))
            if (const auto index = playback_.playlist().index_of(*id)) {
                static_cast<void>(playback_.play_track(*index));
                queue_.save();
                publish({{}, true, playback_.open_result()});
            }
        return;
    }
    if (command == TrackCommand::play || command == TrackCommand::append ||
        command == TrackCommand::apply_order) {
        std::vector<std::filesystem::path> paths;
        if (command == TrackCommand::apply_order)
            paths = std::move(context.displayed_paths);
        else
            for (const auto& selected : context.selection)
                if (!selected.location.empty())
                    paths.push_back(selected.location);
        if (paths.empty()) {
            publish({"Select playable tracks for this command"});
            return;
        }
        open(std::move(paths), command == TrackCommand::append);
        return;
    }
    const auto source =
        context.document.playlist() ? std::optional{*context.document.playlist()} : std::nullopt;
    const bool moving = command == TrackCommand::move;
    if (moving && !source)
        return;
    std::optional<library::PlaylistId> destination = library_->active_playlist().id();
    if (moving || command == TrackCommand::transfer_destination) {
        std::vector<PlaylistChoice> choices;
        for (const auto& playlist : library_->repository().playlists())
            if (!moving || playlist.id != *source)
                choices.push_back({playlist.id, playlist.name});
        const QPointer<DocumentCommandRouter> alive{this};
        destination = dialogs_.destination(choices);
        if (!alive || !library_ || !destination)
            return;
    }
    if (context.selection.empty()) {
        publish({moving ? "Select tracks to move" : "Select tracks to add"});
        return;
    }
    if (!destination) {
        publish({"Choose an active playlist first"});
        return;
    }
    app::PlaylistTransfer transfer{library_->repository(), library_->active_playlist(),
        library_->persistence()};
    app::PlaylistTransferResult result;
    if (moving) {
        std::vector<library::PlaylistEntryId> entries;
        for (const auto& selected : context.selection)
            if (const auto* id = std::get_if<library::PlaylistEntryId>(&selected.occurrence))
                entries.push_back(*id);
        result = transfer.move_to(*source, *destination, entries);
    } else {
        std::vector<app::MediaTransfer> media;
        for (const auto& selected : context.selection)
            media.push_back({selected.media, selected.location});
        result = transfer.add_to(*destination, media);
    }
    publish({result.succeeded
            ? QString{moving ? "%1 track(s) moved" : "%1 track(s) added"}.arg(result.added)
            : QString{"Could not save the playlist transfer"}});
}

} // namespace wiola::gui
