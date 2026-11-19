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

} // namespace

StandardMetadataCommands::StandardMetadataCommands(library::InMemoryRepository& repository,
    Persist persist)
    : repository_{repository}
    , persist_{std::move(persist)}
{
}

StandardMetadataResult StandardMetadataCommands::apply(const library::MediaId& media,
    const library::StandardFieldOverrides& edit)
{
    const library::TrackRecord* existing = repository_.track(media);
    if (existing == nullptr)
        return {.error = StandardMetadataError::missing_media, .changed = false};

    if (empty(edit))
        return {.error = StandardMetadataError::empty_edit, .changed = false};

    library::TrackRecord updated = *existing;
    merge(updated.overrides.title, edit.title);
    merge(updated.overrides.artist, edit.artist);
    merge(updated.overrides.album, edit.album);
    merge(updated.overrides.genre, edit.genre);
    merge(updated.overrides.comment, edit.comment);
    merge(updated.overrides.composer, edit.composer);
    merge(updated.overrides.year, edit.year);
    merge(updated.overrides.track_number, edit.track_number);
    merge(updated.overrides.disc_number, edit.disc_number);

    if (updated.overrides == existing->overrides)
        return {.error = StandardMetadataError::none, .changed = false};

    const auto previous_tracks = repository_.tracks();
    const auto previous_playlists = repository_.playlists();
    const auto previous_workspaces = repository_.workspaces();
    ++updated.metadata_revision;

    if (!repository_.put_track(std::move(updated)))
        return {.error = StandardMetadataError::missing_media, .changed = false};

    if (persist_(repository_))
        return {.error = StandardMetadataError::none, .changed = true};

    static_cast<void>(repository_.replace_all(previous_tracks, previous_playlists,
        previous_workspaces));
    return {.error = StandardMetadataError::persistence_failed, .changed = false};
}

} // namespace wiola::app
