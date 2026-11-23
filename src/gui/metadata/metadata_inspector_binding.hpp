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
#include "track_inspector.hpp"
#include <QObject>
#include <QPointer>
#include <app/playback/playback_service.hpp>
#include <desktop/library/library_session.hpp>
#include <gui/shell/document_host.hpp>

namespace wiola::gui {

/// Owns metadata editor command wiring; reparenting the inspector never retains a window.
class MetadataInspectorBinding final : public QObject {
public:
    using Status = std::function<void(const QString&)>;
    MetadataInspectorBinding(TrackInspector& inspector, DocumentHost& host,
        app::LibrarySession& library, app::PlaybackService& playback, Status status);
    void refresh();
    void refresh(const library::LibraryChangeBatch& batch);

private:
    void show_status(const QString& message)
    {
        if (status_)
            status_(message);
    }

    void finish_custom_tag(app::CustomTagResult result, const QString& success);
    void finish_standard_metadata(const app::MetadataEditSession& session);
    void publish_metadata_outcome(const app::MetadataEditOutcome& outcome);
    void resolve_metadata_conflict(MetadataConflictAction action);
    QPointer<TrackInspector> inspector_;
    QPointer<DocumentHost> host_;
    QPointer<app::LibrarySession> library_;
    app::PlaybackService& playback_;
    Status status_;
};

} // namespace wiola::gui
