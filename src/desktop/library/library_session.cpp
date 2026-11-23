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
#include "library_persistence.hpp"
#include <QFileInfo>
#include <QThread>
#include <algorithm>
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
    std::shared_ptr<media::MediaProvider> provider, QObject* parent,
    persistence::qt::AtomicFileStore::OutputFactory output)
    : QObject{parent}
    , settings_{QSettings::IniFormat, QSettings::UserScope, "wiola-player", "settings"}
    , store_{std::move(file), std::move(output)}
    , persist_{[this](const library::LibraryRepository& records) {
        check_owner();
        if (!accepting_ || &records != &repository_ || !persistence_worker_ ||
            restore_result_.schema == persistence::qt::LibraryJsonError::unsupported_version)
            return library::CommitResult{false};
        return persistence_worker_->accept(repository_.staged_delta());
    }}
    , playlists_{repository_, persist_}
    , tags_{repository_, persist_}
    , edits_{repository_, persist_,
          [this](const MetadataEditOutcome& outcome) { emit metadata_outcome(outcome); }}
    , scans_{*this, [this](WorkspaceScanBatch batch) { publish_scan(std::move(batch)); }}
    , writes_{*this, provider, repository_, persist_,
          [this](const MetadataWriteResult& result) { emit file_write_outcome(result); }}
    , metadata_{*this, provider,
          [this](BackgroundMetadataResult result) { publish_metadata(std::move(result)); }}
{
    restore_result_ = persistence::qt::restore_repository(store_, repository_);
    if (!restore_result_.succeeded())
        restore_error_ = "Could not restore the saved library";
    persistence_worker_ = std::make_unique<LibraryPersistence>(*this, store_,
        library::snapshot_of(repository_), [this](std::uint64_t revision, auto result) {
            if (!result.succeeded() && persistence_worker_->durable_revision() < revision) {
                save_failed_ = true;
                emit status_changed(
                    "Library changes remain in memory; saving failed. Use Retry saving library."
                );
            } else if (save_failed_ &&
                persistence_worker_->durable_revision() >=
                    persistence_worker_->pending_revision()) {
                save_failed_ = false;
                emit status_changed("Pending library changes saved");
            }
            emit library_save_finished(revision, result.succeeded());
        });
    writes_.set_durability([this](
                               std::uint64_t revision, auto completed
                           ) { persistence_worker_->when_durable(revision, std::move(completed)); },
        [this] { static_cast<void>(persistence_worker_->flush()); });
    active_.on_changed([this](auto id) { emit active_destination_changed(id); });
    const library::PlaylistId active{settings_.value("playlists/active").toString().toStdString()};
    if (repository_.playlist(active))
        static_cast<void>(active_.set(active));
    for (const auto& track : repository_.tracks()) {
        search_.upsert(track);
        tag_keys_.upsert(track);
        locations_.upsert(track);
        if (!track.locations.empty())
            request_metadata(track.id, library::preferred_location(track));
    }
    changes_ = repository_.observe([this](const library::LibraryChangeBatch& batch) {
        index_changes(batch);
    });
    workspace_extensions_ = provider->playback_extensions();
    watcher_.on_status([this](WorkspaceWatchCoverage coverage) {
        if (coverage.partial && !coverage.awaiting_discovery)
            emit status_changed(coverage.root_missing
                    ? "Folder unavailable; waiting for it to reappear"
                    : "Automatic folder refresh has partial coverage. Use Rescan to refresh "
                      "manually.");
    });
    watcher_.on_changed([this](library::WorkspaceId id) {
        if (accepting_ && repository_.workspace(id))
            static_cast<void>(scan_workspace(id, workspace_extensions_));
        emit workspace_changed(id);
    });
    for (const auto& workspace : repository_.workspaces()) {
        static_cast<void>(watcher_.watch(workspace));
        static_cast<void>(scan_workspace(workspace.id, workspace_extensions_));
    }
}

LibrarySession::~LibrarySession()
{
    static_cast<void>(shutdown());
}

persistence::qt::LibraryStoreResult LibrarySession::shutdown()
{
    check_owner();
    if (!accepting_ && shutdown_result_.succeeded())
        return shutdown_result_;
    writes_.stop();
    accepting_ = false;
    watcher_.stop();
    scans_.stop();
    pending_scans_.clear();
    metadata_.stop();
    shutdown_result_ = !restore_result_.succeeded() && repository_.change_revision() == 0
        ? restore_result_
        : persistence_worker_->flush();
    if (shutdown_result_.succeeded())
        persistence_worker_->stop();
    settings_.sync();
    if (shutdown_result_.succeeded() && settings_.status() != QSettings::NoError)
        shutdown_result_.io = persistence::qt::FileStoreError::io;
    return shutdown_result_;
}

void LibrarySession::retry_save()
{
    check_owner();
    persistence_worker_->retry();
}

persistence::qt::LibraryStoreResult LibrarySession::flush()
{
    check_owner();
    return persistence_worker_->flush();
}

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

MetadataEditService& LibrarySession::metadata_edits()
{
    check_owner();
    return edits_;
}

ActivePlaylist& LibrarySession::active_playlist()
{
    check_owner();
    return active_;
}

std::optional<library::TrackRecord> LibrarySession::snapshot(const library::MediaId& id) const
{
    check_owner();
    const auto* record = repository_.track(id);
    return record ? std::optional{*record} : std::nullopt;
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

std::uint64_t LibrarySession::scan_workspace(const library::WorkspaceId& id,
    std::vector<std::string> extensions)
{
    check_owner();
    const auto* workspace = repository_.workspace(id);
    if (!accepting_ || !workspace)
        return 0;
    if (!watcher_.coverage(id))
        static_cast<void>(watcher_.watch(*workspace));
    watcher_.scan_started(id);
    const auto generation = scans_.start(*workspace, std::move(extensions), repository_.tracks());
    pending_scans_.insert_or_assign(id, PendingScan{generation, {}});
    emit workspace_scan_started(id, generation);
    return generation;
}

void LibrarySession::cancel_workspace_scan(const library::WorkspaceId& id)
{
    check_owner();
    scans_.cancel(id);
    pending_scans_.erase(id);
}

library::MediaId LibrarySession::media_at(const std::filesystem::path& location) const
{
    check_owner();
    return locations_.find(location).value_or(library::media_id_for(location));
}

void LibrarySession::publish_scan(WorkspaceScanBatch batch)
{
    check_owner();
    const auto pending = pending_scans_.find(batch.workspace);
    if (!accepting_ || pending == pending_scans_.end() ||
        pending->second.generation != batch.generation)
        return;
    pending->second.entries.insert(pending->second.entries.end(), batch.entries.begin(),
        batch.entries.end());
    for (const auto& entry : batch.entries)
        request_metadata(entry.media, entry.location);
    watcher_.directories_discovered(batch.workspace, batch.directories,
        batch.status != WorkspaceScanStatus::running,
        batch.status == WorkspaceScanStatus::complete);
    if (batch.status == WorkspaceScanStatus::complete) {
        const auto entries = std::move(pending->second.entries);
        const auto result = reconciliation_.reconcile(repository_, batch.workspace, entries);
        if (!result.succeeded) {
            emit status_changed("Could not save the workspace scan");
            batch.status = WorkspaceScanStatus::failed;
        }
    }
    const bool finished = batch.status != WorkspaceScanStatus::running;
    if (finished)
        if (const auto current = pending_scans_.find(batch.workspace);
            current != pending_scans_.end() && current->second.generation == batch.generation)
            pending_scans_.erase(current);
    emit scan_batch(batch);
}

WorkspaceWatcher& LibrarySession::watcher()
{
    check_owner();
    return watcher_;
}

void LibrarySession::request_metadata(const std::filesystem::path& path)
{
    request_metadata(media_at(path), path);
}

void LibrarySession::request_metadata(const library::MediaId& media,
    const std::filesystem::path& path)
{
    check_owner();
    if (!accepting_ || !media.valid() || path.empty())
        return;
    const auto* current = repository_.track(media);
    metadata_pending_.insert_or_assign(media, current ? current->content_revision : 0);
    static_cast<void>(metadata_.enqueue(media, path));
}

void LibrarySession::request_artwork(const library::MediaId& id)
{
    check_owner();
    if (!accepting_ || metadata_pending_.contains(id) || artwork_.get(id).known())
        return;
    if (const auto* record = repository_.track(id))
        request_metadata(id, library::preferred_location(*record));
}

void LibrarySession::publish_metadata(BackgroundMetadataResult result)
{
    check_owner();
    if (!accepting_)
        return;
    const auto pending = metadata_pending_.find(result.media);
    if (pending == metadata_pending_.end())
        return;
    const auto* current = repository_.track(result.media);
    const auto revision = current ? current->content_revision : 0;
    if (revision != pending->second) {
        metadata_pending_.erase(pending);
        if (current)
            request_metadata(result.media, library::preferred_location(*current));
        return;
    }
    if (!result.metadata) {
        metadata_pending_.erase(result.media);
        if (artwork_.fail(result.media))
            emit artwork_changed(result.media);
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
            metadata_pending_.erase(result.media);
            emit status_changed("Could not add the discovered track to the library");
            return;
        }
    }
    if (const auto* current = repository_.track(result.media); current &&
        (current->missing ||
            std::ranges::find(current->locations, result.location) == current->locations.end())) {
        auto updated = *current;
        if (std::ranges::find(updated.locations, result.location) == updated.locations.end())
            updated.locations.push_back(result.location);
        updated.missing = false;
        created = repository_.put_track(std::move(updated)) || created;
    }
    const bool merged = merge_.merge(repository_, result.media, *result.metadata);
    if ((created || merged) && !transaction.commit())
        emit status_changed("Could not save the learned metadata");
    auto image = std::make_shared<const media::Artwork>(std::move(result.metadata->artwork));
    const bool changed = artwork_.put(result.media, std::move(image));
    metadata_pending_.erase(result.media);
    if (changed)
        emit artwork_changed(result.media);
}

void LibrarySession::index_changes(const library::LibraryChangeBatch& batch)
{
    for (const auto& change : batch.media)
        if (const auto* track = repository_.track(change.media)) {
            search_.upsert(*track);
            tag_keys_.upsert(*track);
            locations_.upsert(*track);
            if (change.includes(library::MediaChangeFields::content)) {
                artwork_.remove(change.media);
                emit artwork_changed(change.media);
            }
        }
    for (const auto& media : batch.removed_media) {
        search_.remove(media);
        tag_keys_.remove(media);
        locations_.remove(media);
        artwork_.remove(media);
        metadata_pending_.erase(media);
    }
    for (const auto& id : batch.removed_workspaces) {
        cancel_workspace_scan(id);
        watcher_.unwatch(id);
    }
    for (const auto& id : batch.workspaces)
        if (const auto* workspace = repository_.workspace(id)) {
            static_cast<void>(watcher_.watch(*workspace));
            static_cast<void>(scan_workspace(id, workspace_extensions_));
        }
    for (const auto& playlist : batch.removed_playlists)
        static_cast<void>(active_.clear_if(playlist));
}

} // namespace wiola::app
