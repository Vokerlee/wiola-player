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

#include "metadata_conflicts.hpp"
#include "standard_metadata_commands.hpp"

#include <map>

namespace wiola::app {

enum class MetadataEditState {
    queued,
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
};

class OptimisticMetadataEdits final {
public:
    using Persist = StandardMetadataCommands::Persist;
    using Observer = std::function<void(const MetadataEditOutcome&)>;

    OptimisticMetadataEdits(library::InMemoryRepository& repository, Persist persist,
        Observer observer);

    [[nodiscard]] MetadataEditOutcome submit(const std::vector<library::MediaId>& media,
        const library::StandardFieldOverrides& edit,
        const std::map<library::MediaId, std::uint64_t>& expected_revisions = {});
    [[nodiscard]] MetadataEditOutcome submit(const std::vector<library::MediaId>& media,
        const library::StandardFieldOverrides& edit,
        const std::map<library::MediaId, ExpectedMediaState>& expected_states);
    [[nodiscard]] MetadataEditOutcome reset(const std::vector<library::MediaId>& media);

private:
    library::InMemoryRepository& repository_;
    Persist persist_;
    Observer observer_;
};

} // namespace wiola::app
