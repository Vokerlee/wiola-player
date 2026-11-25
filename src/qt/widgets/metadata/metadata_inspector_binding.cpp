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

#include "metadata_inspector_binding.hpp"

#include "inspector_context.hpp"
#include "metadata_edit_presentation.hpp"
#include <algorithm>
#include <media/local/resource.hpp>
#include <presentation/documents/queue_rows.hpp>

namespace wiola::gui {

MetadataInspectorBinding::MetadataInspectorBinding(TrackInspector& inspector, DocumentHost& host,
    app::LibrarySession& library, app::PlaybackService& playback, Status status)
    : QObject{&inspector}
    , inspector_{&inspector}
    , host_{&host}
    , library_{&library}
    , playback_{playback}
    , status_{std::move(status)}
{
    inspector_->custom_tags()->on_add([this](library::MediaId media, std::string key,
                                          std::string value) {
        if (!library_)
            return;
        finish_custom_tag(library_->tags().add(media, std::move(key), std::move(value)),
            "Custom tag added");
    });
    inspector_->custom_tags()->on_edit([this](library::MediaId media, library::TagId tag,
                                           std::string key, std::string value) {
        if (!library_)
            return;
        finish_custom_tag(library_->tags().edit(media, tag, std::move(key), std::move(value)),
            "Custom tag updated");
    });
    inspector_->custom_tags()->on_remove([this](library::MediaId media, library::TagId tag) {
        if (!library_)
            return;
        finish_custom_tag(library_->tags().remove(media, tag), "Custom tag removed");
    });
    inspector_->standard_metadata()->on_apply([this](const app::MetadataEditSession& session) {
        if (library_)
            finish_standard_metadata(session);
    });
    inspector_->standard_metadata()->on_file_write([this](const app::MetadataEditSession& draft) {
        if (!library_)
            return;
        const auto captured = draft;
        const auto outcome = library_->metadata_edits().apply(captured.media(), captured.draft(),
            captured.expectations());
        if (!outcome.succeeded()) {
            inspector_->standard_metadata()->show_status(metadata_edit_message(outcome));
            return;
        }
        static_cast<void>(library_->file_writes().submit(captured));
        inspector_->standard_metadata()->discard();
        refresh();
    });
    connect(library_.data(), &app::LibrarySession::file_write_outcome, this,
        [this](const app::MetadataWriteResult& result) {
            const auto message = metadata_file_message(result);
            inspector_->standard_metadata()->show_file_status(message);
            show_status(message);
        });
    const QPointer<app::LibrarySession> artwork_library{library_.data()};
    inspector_->set_artwork_requester([artwork_library](const auto& id) {
        if (artwork_library)
            artwork_library->request_artwork(id);
    });
    connect(library_.data(), &app::LibrarySession::artwork_changed, inspector_,
        [artwork_library, inspector = inspector_.data()](const auto& media) {
            if (artwork_library)
                inspector->refresh_artwork(media, artwork_library->artwork(media));
        });
    inspector_->conflicts()->on_action([this](MetadataConflictAction action) {
        if (library_)
            resolve_metadata_conflict(action);
    });

    connect(library_.data(), &app::LibrarySession::metadata_outcome, this,
        &MetadataInspectorBinding::publish_metadata_outcome);
}

void MetadataInspectorBinding::finish_custom_tag(app::CustomTagResult result,
    const QString& success)
{
    if (!result.succeeded()) {
        switch (result.error) {
        case app::CustomTagError::missing_media:
            show_status("The track is no longer in the library");
            break;
        case app::CustomTagError::missing_tag:
            show_status("The custom tag is no longer available");
            break;
        case app::CustomTagError::invalid_value:
            show_status("Enter a valid, non-empty custom tag");
            break;
        case app::CustomTagError::duplicate_value:
            show_status("That custom tag already exists");
            break;
        case app::CustomTagError::limit_exceeded:
            show_status("The track already has the maximum number of custom tags");
            break;
        case app::CustomTagError::persistence_failed:
            show_status("Could not save the custom tag change");
            break;
        case app::CustomTagError::none:
            break;
        }

        return;
    }

    if (!result.changed) {
        show_status("No custom tag changes to save");
        return;
    }

    show_status(success);
}

void MetadataInspectorBinding::refresh()
{
    if (!inspector_ || !host_ || !library_)
        return;
    const auto active = host_->registry().active();
    std::optional<TrackRow> playing;
    // Only Now Playing opts into a labeled fallback. An empty saved/workspace view stays empty.
    if (active && active->is_queue())
        if (const auto* current = playback_.playlist().current()) {
            playing = queue_row(*current);
            if (auto path = media::local_path(current->resource()))
                playing->media = library_->media_at(*path);
        }
    inspector_->show_context(inspector_context(*library_, host_->registry().selection(),
        std::move(playing)));
}

void MetadataInspectorBinding::refresh(const library::LibraryChangeBatch& batch)
{
    if (!inspector_)
        return;
    for (const auto& row : inspector_->context().tracks) {
        if (std::ranges::any_of(batch.media,
                [&](const auto& change) { return change.media == row.media; }) ||
            std::ranges::find(batch.removed_media, row.media) != batch.removed_media.end()) {
            refresh();
            return;
        }
    }
}

void MetadataInspectorBinding::finish_standard_metadata(const app::MetadataEditSession& session)
{
    const auto outcome =
        library_->metadata_edits().apply(session.media(), session.draft(), session.expectations());
    if (outcome.succeeded()) {
        inspector_->standard_metadata()->discard();
        refresh();
    }
    if (!outcome.succeeded())
        inspector_->standard_metadata()->show_status(metadata_edit_message(outcome));
}

void MetadataInspectorBinding::resolve_metadata_conflict(MetadataConflictAction action)
{
    if (action == MetadataConflictAction::review)
        return;

    auto* editor = inspector_->standard_metadata();
    const auto session = editor->edit_session();
    if (!session.valid())
        return;
    if (action == MetadataConflictAction::keep) {
        editor->discard();
        inspector_->conflicts()->hide_conflict();
        show_status("Kept current application metadata");
        refresh();
        return;
    }
    const auto outcome = action == MetadataConflictAction::reload
        ? library_->metadata_edits().reset(session.media())
        : library_->metadata_edits().apply(session.media(), session.draft());
    if (outcome.succeeded()) {
        editor->discard();
        refresh();
    }
}

void MetadataInspectorBinding::publish_metadata_outcome(const app::MetadataEditOutcome& outcome)
{
    if (outcome.state == app::MetadataEditState::conflicted)
        inspector_->conflicts()->show_conflict(metadata_edit_message(outcome));
    else
        inspector_->conflicts()->hide_conflict();
    show_status(metadata_edit_message(outcome));
}

} // namespace wiola::gui
