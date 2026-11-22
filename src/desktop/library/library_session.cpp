/**
 * @file
 * @brief Application-owned library state, commands, indexes, and background delivery.
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

#include "library_session.hpp"
#include <QFileInfo>
#include <QThread>
#include <library/location.hpp>
#include <platform/qt/qt_path_adapter.hpp>

namespace wiola::app {

std::filesystem::path default_library_file()
{
    const QSettings settings{QSettings::IniFormat, QSettings::UserScope, "wiola-player",
        "settings"};
    return platform::qt::QtPathAdapter::from_qt(QFileInfo{settings.fileName()}.absolutePath() +
        "/library.json");
}

LibrarySession::LibrarySession(std::shared_ptr<media::MediaProvider> provider, QObject* parent)
    : LibrarySession{default_library_file(), std::move(provider), parent}
{
}

LibrarySession::LibrarySession(std::filesystem::path file,
    std::shared_ptr<media::MediaProvider> provider, QObject* parent)
    : QObject{parent}
    , settings_{QSettings::IniFormat, QSettings::UserScope, "wiola-player", "settings"}
    , store_{std::move(file)}
    , persist_{[this](const library::LibraryRepository& records) {
        check_owner();
        return store_.save(library::snapshot_of(records)).succeeded();
    }}
    , playlists_{repository_, persist_}
    , tags_{repository_, persist_}
    , edits_{repository_, persist_,
          [this](const MetadataEditOutcome& outcome) { emit metadata_outcome(outcome); }}
    , scans_{*this,
          [this](WorkspaceScanBatch batch) {
              for (const auto& path : batch.tracks)
                  request_metadata(path);
              emit scan_batch(batch);
          }}
    , metadata_{*this, std::move(provider),
          [this](BackgroundMetadataResult result) { publish_metadata(std::move(result)); }}
{
    if (!persistence::qt::restore_repository(store_, repository_).succeeded())
        restore_error_ = "Could not restore the saved library";
    const library::PlaylistId active{settings_.value("playlists/active").toString().toStdString()};
    if (repository_.playlist(active))
        static_cast<void>(active_.set(active));
    for (const auto& track : repository_.tracks()) {
        search_.upsert(track);
        if (!track.locations.empty())
            request_metadata(track.locations.front());
    }
    changes_ = repository_.observe([this](const library::LibraryChangeBatch& batch) {
        index_changes(batch);
    });
    watcher_.on_changed([this](library::WorkspaceId id) { emit workspace_changed(id); });
    for (const auto& workspace : repository_.workspaces())
        static_cast<void>(watcher_.watch(workspace));
}

LibrarySession::~LibrarySession() = default;

void LibrarySession::check_owner() const
{
    Q_ASSERT(QThread::currentThread() == thread());
}

library::InMemoryRepository& LibrarySession::repository()
{
    check_owner();
    return repository_;
}

const library::InMemoryRepository& LibrarySession::repository() const
{
    check_owner();
    return repository_;
}

SavedPlaylistCommands& LibrarySession::playlists()
{
    check_owner();
    return playlists_;
}

CustomTagCommands& LibrarySession::tags()
{
    check_owner();
    return tags_;
}

OptimisticMetadataEdits& LibrarySession::metadata_edits()
{
    check_owner();
    return edits_;
}

ActivePlaylist& LibrarySession::active_playlist()
{
    check_owner();
    return active_;
}

const SearchIndex& LibrarySession::search() const
{
    check_owner();
    return search_;
}

const library::RepositoryTransaction::Persist& LibrarySession::persistence() const
{
    check_owner();
    return persist_;
}

WorkspaceScanPublisher& LibrarySession::scans()
{
    check_owner();
    return scans_;
}

WorkspaceWatcher& LibrarySession::watcher()
{
    check_owner();
    return watcher_;
}

void LibrarySession::request_metadata(const std::filesystem::path& path)
{
    check_owner();
    static_cast<void>(metadata_.enqueue(library::media_id_for(path), path));
}

void LibrarySession::publish_metadata(BackgroundMetadataResult result)
{
    check_owner();
    if (!result.metadata) {
        emit status_changed("Could not read metadata for " +
            platform::qt::QtPathAdapter::to_qt(result.location.filename()));
        return;
    }
    library::RepositoryTransaction transaction{repository_, persist_};
    bool created = false;
    if (!repository_.track(result.media)) {
        library::TrackRecord record;
        record.id = result.media;
        record.locations = {result.location};
        created = repository_.put_track(std::move(record));
        if (!created) {
            emit status_changed("Could not add the discovered track to the library");
            return;
        }
    }
    const bool merged = merge_.merge(repository_, result.media, *result.metadata);
    if ((created || merged) && !transaction.commit())
        emit status_changed("Could not save the learned metadata");
}

void LibrarySession::index_changes(const library::LibraryChangeBatch& batch)
{
    for (const auto& change : batch.media)
        if (const auto* track = repository_.track(change.media))
            search_.upsert(*track);
    for (const auto& media : batch.removed_media)
        search_.remove(media);
    for (const auto& playlist : batch.removed_playlists)
        if (active_.clear_if(playlist))
            settings_.remove("playlists/active");
}

} // namespace wiola::app
