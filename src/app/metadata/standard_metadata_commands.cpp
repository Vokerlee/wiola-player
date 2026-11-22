/**
 * @file
 * @brief Transactional application-level standard metadata overrides.
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

#include "standard_metadata_commands.hpp"

#include <library/repository_transaction.hpp>

namespace wiola::app {

StandardMetadataCommands::StandardMetadataCommands(library::InMemoryRepository& repository,
    Persist persist)
    : repository_{repository}
    , persist_{std::move(persist)}
{
}

StandardMetadataResult StandardMetadataCommands::apply(const library::MediaId& media,
    const library::StandardMetadataPatch& edit)
{
    const library::TrackRecord* existing = repository_.track(media);
    if (existing == nullptr)
        return {.error = StandardMetadataError::missing_media, .changed = false};

    if (!library::valid_metadata_patch(edit))
        return {.error = StandardMetadataError::invalid_edit};

    if (library::metadata_patch_empty(edit))
        return {.error = StandardMetadataError::empty_edit, .changed = false};

    library::TrackRecord updated = *existing;
    static_cast<void>(library::apply_metadata_patch(updated.overrides, edit));

    if (updated.overrides == existing->overrides)
        return {.error = StandardMetadataError::none, .changed = false};

    library::RepositoryTransaction transaction{repository_, persist_};
    ++updated.metadata_revision;

    if (!repository_.put_track(std::move(updated)))
        return {.error = StandardMetadataError::missing_media, .changed = false};

    if (transaction.commit())
        return {.error = StandardMetadataError::none, .changed = true};
    return {.error = StandardMetadataError::persistence_failed, .changed = false};
}

} // namespace wiola::app
