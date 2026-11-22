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

#include <library/repository_transaction.hpp>

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
    std::map<library::MediaId, ExpectedMediaState> expected_states;

    for (const auto& [id, revision] : expected_revisions) {
        const library::TrackRecord* track = repository_.track(id);
        expected_states.emplace(id,
            ExpectedMediaState{.metadata_revision = revision,
                .content_revision = track != nullptr ? track->content_revision : 0,
                .fingerprint = {}});
    }

    return submit(media, edit, expected_states);
}

MetadataEditOutcome OptimisticMetadataEdits::submit(const std::vector<library::MediaId>& media,
    const library::StandardFieldOverrides& edit,
    const std::map<library::MediaId, ExpectedMediaState>& expected_states)
{
    if (empty(edit)) {
        MetadataEditOutcome outcome{.state = MetadataEditState::failed,
            .error = StandardMetadataError::empty_edit};
        observer_(outcome);
        return outcome;
    }

    const std::set<library::MediaId> unique{media.begin(), media.end()};
    library::RepositoryTransaction transaction{repository_, persist_};
    const MetadataConflictDetector detector;

    for (const auto& [id, expected] : expected_states) {
        const auto conflict = detector.check(repository_, id, expected);
        if (conflict != MetadataConflictKind::none) {
            MetadataEditOutcome outcome{.state = MetadataEditState::conflicted,
                .conflict = conflict};
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

    if (transaction.commit()) {
        outcome.state = MetadataEditState::succeeded;
        observer_(outcome);
        return outcome;
    }
    outcome.state = MetadataEditState::failed;
    outcome.error = StandardMetadataError::persistence_failed;
    outcome.changed = 0;
    observer_(outcome);
    return outcome;
}

MetadataEditOutcome OptimisticMetadataEdits::reset(const std::vector<library::MediaId>& media)
{
    const std::set<library::MediaId> unique{media.begin(), media.end()};
    library::RepositoryTransaction transaction{repository_, persist_};
    MetadataEditOutcome outcome;

    for (const library::MediaId& id : unique) {
        const library::TrackRecord* existing = repository_.track(id);
        if (existing == nullptr) {
            ++outcome.missing;
            continue;
        }

        if (existing->overrides == library::StandardFieldOverrides{}) {
            ++outcome.unchanged;
            continue;
        }

        library::TrackRecord updated = *existing;
        updated.overrides = {};
        ++updated.metadata_revision;

        if (!repository_.put_track(std::move(updated))) {
            ++outcome.missing;
            continue;
        }

        ++outcome.changed;
    }

    outcome.state = MetadataEditState::queued;
    observer_(outcome);

    if (transaction.commit()) {
        outcome.state = MetadataEditState::succeeded;
        observer_(outcome);
        return outcome;
    }
    outcome.state = MetadataEditState::failed;
    outcome.error = StandardMetadataError::persistence_failed;
    outcome.changed = 0;
    observer_(outcome);
    return outcome;
}

} // namespace wiola::app
