/**
 * @file
 * @brief Transactional standard metadata edits across multiple tracks.
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

#include "bulk_metadata_commands.hpp"

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

BulkMetadataCommands::BulkMetadataCommands(library::InMemoryRepository& repository, Persist persist)
    : repository_{repository}
    , persist_{std::move(persist)}
{
}

BulkMetadataResult BulkMetadataCommands::apply(const std::vector<library::MediaId>& media,
    const library::StandardFieldOverrides& edit)
{
    if (empty(edit))
        return {.error = StandardMetadataError::empty_edit};

    const std::set<library::MediaId> unique{media.begin(), media.end()};
    library::RepositoryTransaction transaction{repository_, persist_};
    BulkMetadataResult result;

    for (const library::MediaId& id : unique) {
        const library::TrackRecord* existing = repository_.track(id);
        if (existing == nullptr) {
            ++result.missing;
            continue;
        }

        library::TrackRecord updated = *existing;
        merge(updated.overrides, edit);

        if (updated.overrides == existing->overrides) {
            ++result.unchanged;
            continue;
        }

        ++updated.metadata_revision;
        if (!repository_.put_track(std::move(updated))) {
            ++result.missing;
            continue;
        }

        ++result.changed;
    }

    if (result.changed == 0)
        return result;

    if (transaction.commit())
        return result;
    result.error = StandardMetadataError::persistence_failed;
    result.changed = 0;
    return result;
}

} // namespace wiola::app
