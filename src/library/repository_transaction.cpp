/**
 * @file
 * @brief Owner-thread repository transactions with complete rollback.
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

#include "repository_transaction.hpp"

#include <stdexcept>
#include <type_traits>
#include <utility>

namespace wiola::library {
namespace {

MediaChange changed_media(const TrackRecord* before, const TrackRecord& after)
{
    MediaChange result{.media = after.id,
        .metadata_revision = after.metadata_revision,
        .content_revision = after.content_revision};

    if (!before) {
        result.fields = MediaChangeFields::all;
        return result;
    }

    const auto add = [&result](bool changed, MediaChangeFields field) {
        if (changed)
            result.fields = result.fields | field;
    };
    add(before->embedded != after.embedded, MediaChangeFields::embedded);
    add(before->overrides != after.overrides, MediaChangeFields::overrides);
    add(before->tags != after.tags, MediaChangeFields::tags);
    add(before->locations != after.locations, MediaChangeFields::locations);
    add(before->missing != after.missing, MediaChangeFields::availability);
    add(before->fingerprint != after.fingerprint ||
            before->content_revision != after.content_revision,
        MediaChangeFields::content);
    add(before->metadata_revision != after.metadata_revision, MediaChangeFields::revisions);

    return result;
}

template<typename Map, typename Undo, typename Id>
void touch(Map& records, Undo& undo, const Id& id)
{
    if (undo.contains(id))
        return;

    const auto current = records.find(id);

    if (current == records.end()) {
        undo.try_emplace(id);
        return;
    }

    auto candidate = current->second;
    auto [entry, inserted] = undo.try_emplace(id);
    entry->second = records.extract(current);

    try {
        records.emplace(id, std::move(candidate));
    } catch (...) {
        records.insert(std::move(entry->second));
        undo.erase(entry);
        throw;
    }
}

template<typename Map, typename Undo, typename Changed, typename Removed, typename Convert>
void difference(const Map& after, const Undo& undo, Changed& changed, Removed& removed,
    Convert convert)
{
    for (const auto& [id, before] : undo) {
        const auto current = after.find(id);
        if (current == after.end()) {
            if (before)
                removed.push_back(id);
        } else if (!before || before.mapped() != current->second)
            changed.push_back(convert(before ? &before.mapped() : nullptr, current->second));
    }
}

template<typename Map, typename Undo>
void restore_records(Map& records, Undo& undo) noexcept
{
    for (auto& [id, before] : undo) {
        records.erase(id);
        if (before)
            records.insert(std::move(before));
    }
    undo.clear();
}

} // namespace

RepositoryTransaction::RepositoryTransaction(InMemoryRepository& repository, Persist persist,
    CommitPriority priority)
    : repository_{repository}
    , allocators_{repository.data_.next_playlist_, repository.data_.next_entry_,
          repository.data_.next_workspace_, repository.data_.next_tag_}
    , persist_{std::move(persist)}
    , priority_{priority}
{
    repository_.changes_.check_owner();

    if (!persist_)
        throw std::invalid_argument{"a transaction requires an explicit persistence policy"};

    if (repository_.transaction_)
        throw std::logic_error{"nested library mutation before commit"};

    repository_.transaction_ = this;
}

RepositoryTransaction::~RepositoryTransaction()
{
    restore();
}

void RepositoryTransaction::touch_track(const MediaId& id)
{
    touch(repository_.data_.tracks_, tracks_, id);
}

void RepositoryTransaction::touch_playlist(const PlaylistId& id)
{
    touch(repository_.data_.playlists_, playlists_, id);
}

void RepositoryTransaction::touch_workspace(const WorkspaceId& id)
{
    touch(repository_.data_.workspaces_, workspaces_, id);
}

void RepositoryTransaction::check_mutation() const
{
    if (phase_ != Phase::editing)
        throw std::logic_error{"catalog mutation during transaction commit"};
}

CommitResult RepositoryTransaction::commit()
{
    if (phase_ != Phase::editing)
        throw std::logic_error{"transaction is committing or already completed"};

    phase_ = Phase::committing;

    try {
        auto delta = std::make_shared<RevisionDelta>();
        delta->priority = priority_;

        auto& batch = delta->changes;
        difference(repository_.data_.tracks_, tracks_, batch.media, batch.removed_media,
            changed_media);

        const auto identity = [](const auto*, const auto& record) {
            return record.id;
        };
        difference(repository_.data_.playlists_, playlists_, batch.playlists,
            batch.removed_playlists, identity);
        difference(repository_.data_.workspaces_, workspaces_, batch.workspaces,
            batch.removed_workspaces, identity);

        if (batch.empty()) {
            phase_ = Phase::finished;
            repository_.transaction_ = nullptr;

            return true;
        }

        batch.revision = repository_.change_revision_ + 1;

        for (const auto& change : batch.media)
            delta->tracks.push_back(*repository_.track(change.media));

        for (const auto& id : batch.playlists)
            delta->playlists.push_back(*repository_.playlist(id));

        for (const auto& id : batch.workspaces)
            delta->workspaces.push_back(*repository_.workspace(id));

        repository_.staged_delta_ = delta;

        auto result = persist_(repository_);

        repository_.staged_delta_.reset();

        if (!result) {
            restore();
            return result;
        }

        result.revision = batch.revision;
        repository_.change_revision_ = batch.revision;
        phase_ = Phase::finished;
        repository_.transaction_ = nullptr;
        repository_.changes_.publish(batch);

        return result;
    } catch (...) {
        restore();
        throw;
    }
}

void RepositoryTransaction::restore() noexcept
{
    if (phase_ == Phase::finished)
        return;

    restore_records(repository_.data_.tracks_, tracks_);
    restore_records(repository_.data_.playlists_, playlists_);
    restore_records(repository_.data_.workspaces_, workspaces_);
    repository_.data_.next_playlist_ = allocators_[0];
    repository_.data_.next_entry_ = allocators_[1];
    repository_.data_.next_workspace_ = allocators_[2];
    repository_.data_.next_tag_ = allocators_[3];
    repository_.staged_delta_.reset();
    repository_.transaction_ = nullptr;
    phase_ = Phase::finished;
}

} // namespace wiola::library
