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
#include <app/library/location_index.hpp>
#include <app/library/search_index.hpp>
#include <app/library/tag_key_catalog.hpp>
#include <app/metadata/custom_tag_commands.hpp>
#include <app/metadata/metadata_edit_service.hpp>
#include <app/metadata/metadata_merge.hpp>
#include <app/playback/active_playlist.hpp>
#include <app/workspace/workspace_reconciliation.hpp>
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
    [[nodiscard]] MetadataEditService& metadata_edits();
    [[nodiscard]] ActivePlaylist& active_playlist();
    /// Owned, owner-thread snapshot safe across repository transactions.
    [[nodiscard]] std::optional<library::TrackRecord> snapshot(const library::MediaId& id) const;
    [[nodiscard]] const SearchIndex& search() const;

    [[nodiscard]] const TagKeyCatalog& tag_keys() const
    {
        check_owner();
        return tag_keys_;
    }

    [[nodiscard]] const library::RepositoryTransaction::Persist& persistence() const;
    std::uint64_t scan_workspace(const library::WorkspaceId& id,
        std::vector<std::string> extensions);
    void cancel_workspace_scan();
    [[nodiscard]] library::MediaId media_at(const std::filesystem::path& location) const;
    [[nodiscard]] WorkspaceWatcher& watcher();

    [[nodiscard]] const QString& restore_error() const noexcept { return restore_error_; }

    void request_metadata(const std::filesystem::path& path);
    void request_metadata(const library::MediaId& media, const std::filesystem::path& path);

signals:
    void scan_batch(const wiola::app::WorkspaceScanBatch& batch);
    void metadata_outcome(const wiola::app::MetadataEditOutcome& outcome);
    void status_changed(const QString& status);
    void workspace_changed(const wiola::library::WorkspaceId& id);

private:
    void check_owner() const;
    void publish_metadata(BackgroundMetadataResult result);
    void publish_scan(WorkspaceScanBatch batch);
    void index_changes(const library::LibraryChangeBatch& batch);

    bool accepting_{true};
    persistence::qt::LibraryStoreResult shutdown_result_;
    QSettings settings_;
    persistence::qt::LibraryStore store_;
    library::InMemoryRepository repository_;
    library::RepositoryTransaction::Persist persist_;
    SavedPlaylistCommands playlists_;
    CustomTagCommands tags_;
    MetadataEditService edits_;
    ActivePlaylist active_;
    SearchIndex search_;
    LocationIndex locations_;
    TagKeyCatalog tag_keys_;
    MetadataMerge merge_;
    WorkspaceReconciliation reconciliation_{persist_};

    struct PendingScan {
        library::WorkspaceId workspace;
        std::uint64_t generation;
        std::vector<WorkspaceScanEntry> entries;
    };

    std::optional<PendingScan> pending_scan_;
    QString restore_error_;
    persistence::qt::LibraryStoreResult restore_result_;
    library::LibraryChanges::Subscription changes_;
    WorkspaceScanPublisher scans_;
    MediaRevisionPublisher metadata_;
    WorkspaceWatcher watcher_;
};

[[nodiscard]] std::filesystem::path default_library_file();

} // namespace wiola::app
