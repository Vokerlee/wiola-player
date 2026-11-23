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

#include "metadata_edit_service.hpp"

#include <set>

#include <library/repository_transaction.hpp>

namespace wiola::app {

MetadataEditService::MetadataEditService(library::InMemoryRepository& repository, Persist persist,
    Observer observer)
    : repository_{repository}
    , persist_{std::move(persist)}
    , observer_{std::move(observer)}
{
}

MetadataEditOutcome MetadataEditService::apply(const std::vector<library::MediaId>& media,
    const library::StandardMetadataPatch& edit,
    const std::map<library::MediaId, ExpectedMediaState>& expected_states)
{
    if (!library::valid_metadata_patch(edit)) {
        MetadataEditOutcome outcome{.state = MetadataEditState::failed,
            .error = StandardMetadataError::invalid_edit};
        publish(outcome);
        return outcome;
    }
    if (library::metadata_patch_empty(edit)) {
        MetadataEditOutcome outcome{.state = MetadataEditState::failed,
            .error = StandardMetadataError::empty_edit};
        publish(outcome);
        return outcome;
    }

    const std::set<library::MediaId> unique{media.begin(), media.end()};
    library::RepositoryTransaction transaction{repository_, persist_};
    const MetadataConflictDetector detector;

    for (const auto& [id, expected] : expected_states) {
        if (!unique.contains(id))
            continue;
        const auto conflict = detector.check(repository_, id, expected);
        if (conflict != MetadataConflictKind::none) {
            MetadataEditOutcome outcome{.state = MetadataEditState::conflicted,
                .conflict = conflict};
            publish(outcome);
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
        static_cast<void>(library::apply_metadata_patch(updated.overrides, edit));

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

    if (outcome.changed == 0) {
        outcome.state = MetadataEditState::succeeded;
        publish(outcome);
        return outcome;
    }
    outcome.state = MetadataEditState::queued;
    publish(outcome);

    const auto committed = transaction.commit();
    if (committed) {
        outcome.revision = committed.revision;
        outcome.state =
            committed.durable() ? MetadataEditState::succeeded : MetadataEditState::accepted;
        outcome.retained_changes = !committed.durable();
        publish(outcome);
        if (committed.receipt) {
            committed.receipt->observe(
                [outcome, observer = observer_](library::DurabilityEvent event) mutable {
                    outcome.state =
                        event.durable ? MetadataEditState::succeeded : MetadataEditState::failed;
                    outcome.error = event.durable ? StandardMetadataError::none
                                                  : StandardMetadataError::persistence_failed;
                    if (observer)
                        observer(outcome);
                }
            );
        }
        return outcome;
    }
    transaction.rollback();
    outcome.state = MetadataEditState::failed;
    outcome.error = StandardMetadataError::persistence_failed;
    outcome.changed = 0;
    publish(outcome);
    return outcome;
}

MetadataEditOutcome MetadataEditService::reset(const std::vector<library::MediaId>& media)
{
    library::StandardMetadataPatch patch;
    patch.title = library::FieldEdit<std::string>::inherit();
    patch.artist = library::FieldEdit<std::string>::inherit();
    patch.album = library::FieldEdit<std::string>::inherit();
    patch.genre = library::FieldEdit<std::string>::inherit();
    patch.comment = library::FieldEdit<std::string>::inherit();
    patch.composer = library::FieldEdit<std::string>::inherit();
    patch.year = library::FieldEdit<std::uint32_t>::inherit();
    patch.track_number = library::FieldEdit<std::uint32_t>::inherit();
    patch.disc_number = library::FieldEdit<std::uint32_t>::inherit();
    return apply(media, patch);
}

void MetadataEditService::publish(const MetadataEditOutcome& outcome) const
{
    if (observer_)
        observer_(outcome);
}

} // namespace wiola::app
