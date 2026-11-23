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

#pragma once
#ifndef Q_MOC_RUN
#include "session_state.hpp"
#include <QPointer>
#include <QSettings>
#include <functional>
#include <persistence/qt/atomic_file_store.hpp>
#endif
#include <QObject>
#include <QTimer>

namespace wiola::app {

class LibrarySession;

/// Owner-thread view preferences with coalesced writes and explicit flush-on-quit.
class SessionPreferences final : public QObject {
    Q_OBJECT
public:
    explicit SessionPreferences(const QSettings& legacy,
        persistence::qt::AtomicFileStore::OutputFactory output = {});
    ~SessionPreferences() override;

    [[nodiscard]] const SessionState& state() const { return state_; }

    [[nodiscard]] SessionStateError restore_error() const { return restore_error_; }

    void edit(const std::function<void(SessionState&)>& change);
    void remap_queue(const std::map<engine::QueueEntryId, engine::QueueEntryId>& mapping);
    void bind_library(LibrarySession& library);
    [[nodiscard]] SessionStateError flush();
signals:
    void save_failed(wiola::app::SessionStateError error);

private:
    SessionState state_;
    QSettings legacy_;
    persistence::qt::AtomicFileStore store_;
    QByteArray last_good_;
    QTimer timer_;
    QPointer<LibrarySession> library_;
    SessionStateError restore_error_{SessionStateError::none};
    std::uint64_t revision_{};
    bool dirty_{}, read_only_{}, migrated_{}, flushing_{};
};

} // namespace wiola::app
