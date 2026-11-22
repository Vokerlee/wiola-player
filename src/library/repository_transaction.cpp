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

template<typename Map, typename Changed, typename Removed, typename Convert>
void difference(const Map& before, const Map& after, Changed& changed, Removed& removed,
    Convert convert)
{
    for (const auto& [id, value] : after) {
        const auto old = before.find(id);
        if (old == before.end() || old->second != value)
            changed.push_back(convert(old == before.end() ? nullptr : &old->second, value));
    }
    for (const auto& [id, value] : before)
        if (!after.contains(id))
            removed.push_back(id);
}

} // namespace

RepositoryTransaction::RepositoryTransaction(InMemoryRepository& repository, Persist persist)
    : repository_{repository}
    , checkpoint_{repository.data_}
    , persist_{std::move(persist)}
{
    repository_.changes_.check_owner();
    if (!persist_)
        throw std::invalid_argument{"a transaction requires an explicit persistence policy"};
    if (repository_.transaction_active_)
        throw std::logic_error{"nested library mutation before commit"};
    repository_.transaction_active_ = true;
}

RepositoryTransaction::~RepositoryTransaction()
{
    rollback();
}

bool RepositoryTransaction::commit()
{
    if (finished_)
        throw std::logic_error{"transaction already completed"};
    LibraryChangeBatch batch;
    difference(checkpoint_.tracks_, repository_.data_.tracks_, batch.media, batch.removed_media,
        changed_media);
    const auto identity = [](const auto*, const auto& record) {
        return record.id;
    };
    difference(checkpoint_.playlists_, repository_.data_.playlists_, batch.playlists,
        batch.removed_playlists, identity);
    difference(checkpoint_.workspaces_, repository_.data_.workspaces_, batch.workspaces,
        batch.removed_workspaces, identity);
    if (batch.empty()) {
        finished_ = true;
        repository_.transaction_active_ = false;
        return true;
    }
    if (!persist_(repository_)) {
        rollback();
        return false;
    }
    batch.revision = ++repository_.change_revision_;
    finished_ = true;
    repository_.transaction_active_ = false;
    repository_.changes_.publish(std::move(batch));
    return true;
}

void RepositoryTransaction::rollback() noexcept
{
    static_assert(std::is_nothrow_move_assignable_v<InMemoryRepository::State>);
    if (finished_)
        return;
    repository_.data_ = std::move(checkpoint_);
    repository_.transaction_active_ = false;
    finished_ = true;
}

} // namespace wiola::library
