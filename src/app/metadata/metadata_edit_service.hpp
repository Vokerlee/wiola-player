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

#pragma once

#include <library/metadata_patch.hpp>

#include "metadata_conflicts.hpp"
#include <library/repository_transaction.hpp>

#include <map>

namespace wiola::app {

enum class StandardMetadataError {
    none,
    missing_media,
    empty_edit,
    invalid_edit,
    persistence_failed,
};

enum class MetadataEditState {
    queued,
    accepted,
    succeeded,
    failed,
    conflicted,
};

struct MetadataEditOutcome {
    MetadataEditState state{MetadataEditState::queued};
    StandardMetadataError error{StandardMetadataError::none};
    MetadataConflictKind conflict{MetadataConflictKind::none};
    std::size_t changed{0};
    std::size_t unchanged{0};
    std::size_t missing{0};

    std::uint64_t revision{};
    bool retained_changes{};

    [[nodiscard]] bool succeeded() const noexcept
    {
        return state == MetadataEditState::accepted || state == MetadataEditState::succeeded;
    }
};

class MetadataEditService final {
public:
    using Persist = library::RepositoryTransaction::Persist;
    using Observer = std::function<void(const MetadataEditOutcome&)>;

    MetadataEditService(library::InMemoryRepository& repository, Persist persist,
        Observer observer = {});

    [[nodiscard]] MetadataEditOutcome apply(const std::vector<library::MediaId>& media,
        const library::StandardMetadataPatch& edit,
        const std::map<library::MediaId, ExpectedMediaState>& expected_states = {});
    [[nodiscard]] MetadataEditOutcome reset(const std::vector<library::MediaId>& media);

private:
    void publish(const MetadataEditOutcome& outcome) const;
    library::InMemoryRepository& repository_;
    Persist persist_;
    Observer observer_;
};

} // namespace wiola::app
