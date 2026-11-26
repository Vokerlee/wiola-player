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
#include <stdexcept>
#include <thread>
#include <utility>

namespace wiola::app {

struct MetadataEditService::State : std::enable_shared_from_this<State> {
    State(library::InMemoryRepository& records, Persist writer, Observer observe,
        MetadataConflictDetector::ReadFingerprint fingerprint)
        : repository{records}
        , persist{std::move(writer)}
        , observer{std::move(observe)}
        , conflicts{std::move(fingerprint)}
    {
        if (!persist)
            throw std::invalid_argument{"metadata edits require an explicit persistence policy"};
    }

    void check_owner() const
    {
        if (std::this_thread::get_id() != owner)
            throw std::logic_error{"metadata edits accessed outside owner thread"};
    }

    void publish(const MetadataEditOutcome& outcome)
    {
        check_owner();
        if (active && observer)
            observer(outcome);
    }

    MetadataEditOutcome edit(const std::vector<library::MediaId>& media,
        const library::StandardMetadataPatch& patch,
        const std::map<library::MediaId, ExpectedMediaState>& expected)
    {
        if (!library::valid_metadata_patch(patch))
            return {.state = MetadataEditState::failed,
                .error = StandardMetadataError::invalid_edit};
        if (library::metadata_patch_empty(patch))
            return {.state = MetadataEditState::failed, .error = StandardMetadataError::empty_edit};
        const std::set<library::MediaId> unique{media.begin(), media.end()};
        library::RepositoryTransaction transaction{repository,
            [this](const auto& candidate) { return persist(candidate); }};
        for (const auto& [id, version] : expected) {
            if (!unique.contains(id))
                continue;
            const auto conflict = conflicts.check(repository, id, version);
            if (conflict != MetadataConflictKind::none)
                return {.state = MetadataEditState::conflicted, .conflict = conflict};
        }
        MetadataEditOutcome outcome;
        for (const auto& id : unique) {
            const auto* existing = repository.track(id);
            if (!existing) {
                ++outcome.missing;
                continue;
            }
            auto updated = *existing;
            static_cast<void>(library::apply_metadata_patch(updated.overrides, patch));
            if (updated.overrides == existing->overrides) {
                ++outcome.unchanged;
                continue;
            }
            ++updated.metadata_revision;
            if (!repository.put_track(std::move(updated))) {
                ++outcome.missing;
                continue;
            }
            ++outcome.changed;
        }
        if (outcome.changed == 0) {
            outcome.state = MetadataEditState::succeeded;
            return outcome;
        }
        const auto committed = transaction.commit();
        if (!committed) {
            outcome.state = MetadataEditState::failed;
            outcome.error = StandardMetadataError::persistence_failed;
            outcome.changed = 0;
            return outcome;
        }
        outcome.revision = committed.revision;
        outcome.state =
            committed.durable() ? MetadataEditState::succeeded : MetadataEditState::accepted;
        outcome.retained_changes = !committed.durable();
        if (committed.receipt) {
            const auto weak = weak_from_this();
            committed.receipt->observe([weak, outcome](library::DurabilityEvent event) mutable {
                if (const auto state = weak.lock()) {
                    outcome.state =
                        event.durable ? MetadataEditState::succeeded : MetadataEditState::failed;
                    outcome.error = event.durable ? StandardMetadataError::none
                                                  : StandardMetadataError::persistence_failed;
                    outcome.retained_changes = !event.durable;
                    state->publish(outcome);
                }
            });
        }
        return outcome;
    }

    library::InMemoryRepository& repository;
    Persist persist;
    Observer observer;
    MetadataConflictDetector conflicts;
    const std::thread::id owner{std::this_thread::get_id()};
    bool active{true};
};

MetadataEditService::MetadataEditService(library::InMemoryRepository& repository, Persist persist,
    Observer observer, MetadataConflictDetector::ReadFingerprint fingerprint)
    : state_{std::make_shared<State>(repository, std::move(persist), std::move(observer),
          std::move(fingerprint))}
{
}

MetadataEditService::~MetadataEditService()
{
    state_->check_owner();
    state_->active = false;
}

MetadataEditOutcome MetadataEditService::apply(const std::vector<library::MediaId>& media,
    const library::StandardMetadataPatch& edit,
    const std::map<library::MediaId, ExpectedMediaState>& expected)
{
    const auto state = state_;
    state->check_owner();
    const auto captured_edit = edit;
    const auto captured_expected = expected;
    const auto outcome = state->edit(media, captured_edit, captured_expected);
    state->publish(outcome);
    return outcome;
}

MetadataEditOutcome MetadataEditService::apply(const MetadataEditSession& session,
    MetadataConflictPolicy policy)
{
    const auto state = state_;
    state->check_owner();
    const auto media = session.media();
    const auto patch = session.draft();
    const auto expected = policy == MetadataConflictPolicy::require_captured_state
        ? session.expectations()
        : std::map<library::MediaId, ExpectedMediaState>{};
    const auto outcome = state->edit(media, patch, expected);
    state->publish(outcome);
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

} // namespace wiola::app
