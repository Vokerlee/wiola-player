/**
 * @file
 * @brief Optimistic standard metadata edits with observable persistence outcomes.
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

#include "optimistic_metadata_edits.hpp"

#include <set>

namespace wiola::app {

namespace {

bool empty(const library::StandardFieldOverrides& edit)
{
    return !edit.title && !edit.artist && !edit.album && !edit.genre && !edit.comment &&
        !edit.composer && !edit.year && !edit.track_number && !edit.disc_number;
}

template<typename T>
void merge(std::optional<T>& current, const std::optional<T>& edit)
{
    if (edit)
        current = *edit;
}

void merge(library::StandardFieldOverrides& current, const library::StandardFieldOverrides& edit)
{
    merge(current.title, edit.title);
    merge(current.artist, edit.artist);
    merge(current.album, edit.album);
    merge(current.genre, edit.genre);
    merge(current.comment, edit.comment);
    merge(current.composer, edit.composer);
    merge(current.year, edit.year);
    merge(current.track_number, edit.track_number);
    merge(current.disc_number, edit.disc_number);
}

} // namespace

OptimisticMetadataEdits::OptimisticMetadataEdits(library::InMemoryRepository& repository,
    Persist persist, Observer observer)
    : repository_{repository}
    , persist_{std::move(persist)}
    , observer_{std::move(observer)}
{
}

MetadataEditOutcome OptimisticMetadataEdits::submit(const std::vector<library::MediaId>& media,
    const library::StandardFieldOverrides& edit,
    const std::map<library::MediaId, std::uint64_t>& expected_revisions)
{
    if (empty(edit)) {
        MetadataEditOutcome outcome{.state = MetadataEditState::failed,
            .error = StandardMetadataError::empty_edit};
        observer_(outcome);
        return outcome;
    }

    const std::set<library::MediaId> unique{media.begin(), media.end()};
    const auto previous_tracks = repository_.tracks();
    const auto previous_playlists = repository_.playlists();
    const auto previous_workspaces = repository_.workspaces();

    for (const auto& [id, revision] : expected_revisions) {
        const library::TrackRecord* track = repository_.track(id);
        if (track != nullptr && track->metadata_revision != revision) {
            MetadataEditOutcome outcome{.state = MetadataEditState::conflicted};
            observer_(outcome);
            return outcome;
        }
    }

    MetadataEditOutcome outcome;

    for (const library::MediaId& id : unique) {
        const library::TrackRecord* existing = repository_.track(id);
        if (existing == nullptr) {
            ++outcome.missing;
            continue;
        }

        library::TrackRecord updated = *existing;
        merge(updated.overrides, edit);

        if (updated.overrides == existing->overrides) {
            ++outcome.unchanged;
            continue;
        }

        ++updated.metadata_revision;
        if (!repository_.put_track(std::move(updated))) {
            ++outcome.missing;
            continue;
        }

        ++outcome.changed;
    }

    outcome.state = MetadataEditState::queued;
    observer_(outcome);

    if (outcome.changed == 0 || persist_(repository_)) {
        outcome.state = MetadataEditState::succeeded;
        observer_(outcome);
        return outcome;
    }

    static_cast<void>(repository_.replace_all(previous_tracks, previous_playlists,
        previous_workspaces));
    outcome.state = MetadataEditState::failed;
    outcome.error = StandardMetadataError::persistence_failed;
    outcome.changed = 0;
    observer_(outcome);
    return outcome;
}

} // namespace wiola::app
