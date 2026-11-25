/**
 * @file
 * @brief Owned inspector selection snapshots and explicit playing fallback.
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

#include "metadata_edit_presentation.hpp"

namespace wiola::gui {

QString metadata_file_message(const app::MetadataWriteResult& result)
{
    switch (result.stage) {
    case app::MetadataWriteStage::queued:
        return "File write queued";
    case app::MetadataWriteStage::remuxing:
        return "Staging file metadata; the original remains usable";
    case app::MetadataWriteStage::validating:
        return "Validating unchanged audio";
    case app::MetadataWriteStage::ready:
        return "Staged file validated";
    case app::MetadataWriteStage::reloading:
        return "Replacing the file and reloading playback";
    case app::MetadataWriteStage::complete:
        return "Saved embedded metadata to file";
    case app::MetadataWriteStage::conflicted:
        return "File changed; embedded metadata was not overwritten. Application overrides remain.";
    case app::MetadataWriteStage::cancelled:
        return "File write cancelled. Application overrides remain.";
    case app::MetadataWriteStage::failed:
        if (result.outcome.source_modified)
            return QString::fromStdString(result.outcome.detail);
        if (result.outcome.error == media::MetadataWriteError::unsupported)
            return "This format does not support the requested file edit. Application overrides "
                   "remain.";
        return "Could not write embedded metadata. Application overrides remain. " +
            QString::fromStdString(result.outcome.detail);
    }
    return {};
}

QString metadata_edit_message(const app::MetadataEditOutcome& outcome)
{
    switch (outcome.state) {
    case app::MetadataEditState::accepted:
        return QString{"Application metadata updated; saving revision %1"}.arg(outcome.revision);
    case app::MetadataEditState::conflicted:
        return "The track changed since editing started; review before applying";
    case app::MetadataEditState::failed:
        if (outcome.retained_changes)
            return QString{"Application metadata remains in memory; revision %1 could not be "
                           "saved. Retry saving the library."}
                .arg(outcome.revision);
        switch (outcome.error) {
        case app::StandardMetadataError::empty_edit:
            return "There are no metadata changes to apply";
        case app::StandardMetadataError::invalid_edit:
            return "The metadata values are invalid";
        default:
            return "Could not save the metadata changes";
        }
    case app::MetadataEditState::succeeded:
        if (outcome.changed == 0)
            return outcome.missing ? "No selected tracks are available in the library"
                                   : "No metadata changes to save";
        return outcome.missing
            ? QString{"Application metadata updated for %1 track(s); %2 unavailable"}
                  .arg(outcome.changed)
                  .arg(outcome.missing)
            : QString{"Application metadata updated for %1 track(s)"}.arg(outcome.changed);
    }
    return {};
}

} // namespace wiola::gui
