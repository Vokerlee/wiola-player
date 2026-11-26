/**
 * @file
 * @brief Application-owned playback preferences and one-time restoration.
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

#include "session_preferences.hpp"
#include <QDir>
#include <QFileInfo>
#include <QScopedValueRollback>
#include <QThread>
#include <qt/application/library/library_session.hpp>
#include <qt/paths.hpp>
#include <stdexcept>

namespace wiola::app {
namespace {

SessionState legacy_state(const QSettings& settings)
{
    SessionState state;
    for (const auto& id : settings.value("documents/open_playlists").toStringList())
        if (!id.isEmpty())
            state.documents.open.push_back(gui::DocumentId::playlist(library::PlaylistId{
                id.toStdString()}));
    for (const auto& id : settings.value("documents/open_workspaces").toStringList())
        if (!id.isEmpty())
            state.documents.open.push_back(gui::DocumentId::workspace(library::WorkspaceId{
                id.toStdString()}));
    const auto kind = settings.value("documents/current_kind").toString();
    const auto current = settings.value("documents/current_id").toString().toStdString();
    if (kind == "empty")
        state.documents.visible.reset();
    else if (kind == "playlist" && !current.empty())
        state.documents.visible = gui::DocumentId::playlist(library::PlaylistId{current});
    else if (kind == "workspace" && !current.empty())
        state.documents.visible = gui::DocumentId::workspace(library::WorkspaceId{current});
    else if (kind.isEmpty()) {
        const auto workspace =
                       settings.value("documents/current_workspace").toString().toStdString(),
                   playlist = settings.value("documents/current_playlist").toString().toStdString();
        if (!workspace.empty())
            state.documents.visible = gui::DocumentId::workspace(library::WorkspaceId{workspace});
        else if (!playlist.empty())
            state.documents.visible = gui::DocumentId::playlist(library::PlaylistId{playlist});
    }
    const auto destination = settings.value("playlists/active").toString().toStdString();
    if (!destination.empty())
        state.documents.destination = library::PlaylistId{destination};
    return state;
}

} // namespace

SessionPreferences::SessionPreferences(const QSettings& legacy,
    persistence::qt::AtomicFileStore::OutputFactory output)
    : legacy_{legacy.fileName(), QSettings::IniFormat}
    , store_{platform::qt::QtPathAdapter::from_qt(
                 QFileInfo{legacy.fileName()}.dir().filePath("session.json")
             ),
          2 * 1024 * 1024, std::move(output)}
{
    bool primary_exists = false;
    for (const auto slot :
        {persistence::qt::FileSlot::primary, persistence::qt::FileSlot::backup}) {
        const auto read = store_.read(slot);
        if (slot == persistence::qt::FileSlot::primary)
            primary_exists = read.exists;
        if (!read.exists)
            continue;
        SessionState candidate;
        const auto error = read.error == persistence::qt::FileStoreError::none
            ? read_session_state(read.bytes, candidate)
            : SessionStateError::io;
        if (error == SessionStateError::none) {
            state_ = std::move(candidate);
            last_good_ = read.bytes;
            break;
        }
        if (error == SessionStateError::unsupported_version)
            read_only_ = true;
        if (restore_error_ == SessionStateError::none ||
            error == SessionStateError::unsupported_version)
            restore_error_ = error;
    }
    if (last_good_.isEmpty()) {
        if (!primary_exists && !read_only_) {
            state_ = legacy_state(legacy_);
            if (write_session_state(state_).isEmpty())
                state_ = {};
            migrated_ = true;
        }
        last_good_ = write_session_state(state_);
    }
    if (migrated_) {
        dirty_ = true;
        ++revision_;
    }
    timer_.setSingleShot(true);
    timer_.setInterval(250);
    connect(&timer_, &QTimer::timeout, this, [this] { static_cast<void>(flush()); });
}

SessionPreferences::~SessionPreferences()
{
    static_cast<void>(flush());
}

void SessionPreferences::edit(const std::function<void(SessionState&)>& change)
{
    Q_ASSERT(QThread::currentThread() == thread());
    auto candidate = state_;
    change(candidate);
    if (candidate == state_)
        return;
    state_ = std::move(candidate);
    dirty_ = true;
    ++revision_;
    timer_.start();
}

void SessionPreferences::remap_queue(
    const std::map<engine::QueueEntryId, engine::QueueEntryId>& mapping
)
{
    edit([&](auto& state) { state.documents.remap_queue(mapping); });
}

void SessionPreferences::bind_library(LibrarySession& library)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (library_ == &library)
        return;
    if (library_)
        throw std::logic_error{"preferences already belong to another library session"};
    library_ = &library;
    if (state_.documents.destination &&
        library.repository().playlist(*state_.documents.destination))
        static_cast<void>(library.playlist_destination().set(*state_.documents.destination));
    else {
        if (library.playlist_destination().id())
            static_cast<void>(
                library.playlist_destination().clear_if(*library.playlist_destination().id())
            );
        if (state_.documents.destination)
            edit([](auto& state) { state.documents.destination.reset(); });
    }
    connect(&library, &LibrarySession::active_destination_changed, this,
        [this](const std::optional<library::PlaylistId>& destination) {
            edit([&](auto& state) { state.documents.destination = destination; });
        });
}

SessionStateError SessionPreferences::flush()
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (flushing_)
        return SessionStateError::busy;
    const QScopedValueRollback<bool> in_progress{flushing_, true};
    timer_.stop();
    if (!dirty_)
        return SessionStateError::none;
    const auto fail = [this](SessionStateError error) {
        emit save_failed(error);
        return error;
    };
    if (read_only_)
        return fail(SessionStateError::unsupported_version);
    const auto revision = revision_;
    const auto bytes = write_session_state(state_);
    if (bytes.isEmpty())
        return fail(SessionStateError::invalid);
    using namespace persistence::qt;
    if (store_.write(FileSlot::backup, last_good_) != FileStoreError::none ||
        store_.write(FileSlot::primary, bytes) != FileStoreError::none)
        return fail(SessionStateError::io);
    last_good_ = bytes;
    dirty_ = revision_ != revision;
    if (dirty_)
        timer_.start();
    if (migrated_) {
        legacy_.remove("documents");
        legacy_.remove("playlists/active");
        legacy_.sync();
        migrated_ = false;
    }
    return SessionStateError::none;
}

} // namespace wiola::app
