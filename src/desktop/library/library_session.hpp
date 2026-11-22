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

#pragma once
#ifndef Q_MOC_RUN
#include "saved_playlist_commands.hpp"
#include <app/library/search_index.hpp>
#include <app/metadata/custom_tag_commands.hpp>
#include <app/metadata/metadata_merge.hpp>
#include <app/metadata/optimistic_metadata_edits.hpp>
#include <app/playback/active_playlist.hpp>
#include <desktop/metadata/media_revision_publisher.hpp>
#include <desktop/workspace/workspace_scan_publisher.hpp>
#include <desktop/workspace/workspace_watcher.hpp>
#include <persistence/qt/library_store.hpp>
#endif
#include <QObject>
#include <QSettings>

namespace wiola::app {

/// Desktop-owned lifetime boundary. All record access and commands belong to the QObject thread;
/// worker snapshots return through the queued publishers. Windows borrow this session.
class LibrarySession final : public QObject {
    Q_OBJECT
public:
    explicit LibrarySession(std::shared_ptr<media::MediaProvider> provider,
        QObject* parent = nullptr);
    LibrarySession(std::filesystem::path file, std::shared_ptr<media::MediaProvider> provider,
        QObject* parent = nullptr, persistence::qt::AtomicFileStore::OutputFactory output = {});
    ~LibrarySession() override;
    [[nodiscard]] persistence::qt::LibraryStoreResult shutdown();

    [[nodiscard]] library::InMemoryRepository& repository();
    [[nodiscard]] const library::InMemoryRepository& repository() const;
    [[nodiscard]] SavedPlaylistCommands& playlists();
    [[nodiscard]] CustomTagCommands& tags();
    [[nodiscard]] OptimisticMetadataEdits& metadata_edits();
    [[nodiscard]] ActivePlaylist& active_playlist();
    [[nodiscard]] const SearchIndex& search() const;
    [[nodiscard]] const library::RepositoryTransaction::Persist& persistence() const;
    [[nodiscard]] WorkspaceScanPublisher& scans();
    [[nodiscard]] WorkspaceWatcher& watcher();

    [[nodiscard]] const QString& restore_error() const noexcept { return restore_error_; }

    void request_metadata(const std::filesystem::path& path);

signals:
    void scan_batch(const wiola::app::WorkspaceScanBatch& batch);
    void metadata_outcome(const wiola::app::MetadataEditOutcome& outcome);
    void status_changed(const QString& status);
    void workspace_changed(const wiola::library::WorkspaceId& id);

private:
    void check_owner() const;
    void publish_metadata(BackgroundMetadataResult result);
    void index_changes(const library::LibraryChangeBatch& batch);

    bool accepting_{true};
    persistence::qt::LibraryStoreResult shutdown_result_;
    QSettings settings_;
    persistence::qt::LibraryStore store_;
    library::InMemoryRepository repository_;
    library::RepositoryTransaction::Persist persist_;
    SavedPlaylistCommands playlists_;
    CustomTagCommands tags_;
    OptimisticMetadataEdits edits_;
    ActivePlaylist active_;
    SearchIndex search_;
    MetadataMerge merge_;
    QString restore_error_;
    persistence::qt::LibraryStoreResult restore_result_;
    library::LibraryChanges::Subscription changes_;
    WorkspaceScanPublisher scans_;
    MediaRevisionPublisher metadata_;
    WorkspaceWatcher watcher_;
};

[[nodiscard]] std::filesystem::path default_library_file();

} // namespace wiola::app
