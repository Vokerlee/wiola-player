/**
 * @file
 * @brief Transactional custom tag add, edit, and remove commands.
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

#include "custom_tag_commands.hpp"

#include <library/tags.hpp>

#include <algorithm>
#include <utility>

namespace wiola::app {

namespace {

bool duplicate(const library::TrackRecord& track, const library::TagValue& candidate,
    const library::TagId& except = {})
{
    const std::string key = library::normalized_tag_text(candidate.key);
    const std::string value = library::normalized_tag_text(candidate.value);

    return std::ranges::any_of(track.tags, [&](const library::TagRecord& tag) {
        return tag.id != except && library::normalized_tag_text(tag.key) == key &&
            library::normalized_tag_text(tag.value) == value;
    });
}

CustomTagResult failure(CustomTagError error, library::TagId tag = {})
{
    return {.error = error, .tag = std::move(tag), .changed = false};
}

} // namespace

CustomTagCommands::CustomTagCommands(library::InMemoryRepository& repository, Persist persist)
    : repository_{repository}
    , persist_{std::move(persist)}
{
}

CustomTagResult CustomTagCommands::add(const library::MediaId& media, std::string key,
    std::string value)
{
    const library::TrackRecord* track = repository_.track(media);
    if (track == nullptr)
        return failure(CustomTagError::missing_media);

    auto candidate = library::make_tag_value(std::move(key), std::move(value));
    if (!candidate)
        return failure(CustomTagError::invalid_value);

    if (duplicate(*track, *candidate))
        return failure(CustomTagError::duplicate_value);

    if (track->tags.size() >= library::max_tags_per_track)
        return failure(CustomTagError::limit_exceeded);

    const Snapshot previous = snapshot();
    const library::TagId added =
        repository_.add_tag(media, std::move(candidate->key), std::move(candidate->value));

    if (!added.valid())
        return failure(CustomTagError::invalid_value);

    return commit(previous, added);
}

CustomTagResult CustomTagCommands::edit(const library::MediaId& media, const library::TagId& tag,
    std::string key, std::string value)
{
    const library::TrackRecord* track = repository_.track(media);
    if (track == nullptr)
        return failure(CustomTagError::missing_media, tag);

    const auto existing = std::ranges::find(track->tags, tag, &library::TagRecord::id);
    if (existing == track->tags.end())
        return failure(CustomTagError::missing_tag, tag);

    auto candidate = library::make_tag_value(std::move(key), std::move(value));
    if (!candidate)
        return failure(CustomTagError::invalid_value, tag);

    if (duplicate(*track, *candidate, tag))
        return failure(CustomTagError::duplicate_value, tag);

    if (existing->key == candidate->key && existing->value == candidate->value)
        return {.error = CustomTagError::none, .tag = tag, .changed = false};

    const Snapshot previous = snapshot();
    library::TrackRecord updated = *track;
    auto& edited =
        updated.tags[static_cast<std::size_t>(std::distance(track->tags.begin(), existing))];
    edited.key = std::move(candidate->key);
    edited.value = std::move(candidate->value);
    ++updated.metadata_revision;

    if (!repository_.put_track(std::move(updated)))
        return failure(CustomTagError::invalid_value, tag);

    return commit(previous, tag);
}

CustomTagResult CustomTagCommands::remove(const library::MediaId& media, const library::TagId& tag)
{
    const library::TrackRecord* track = repository_.track(media);
    if (track == nullptr)
        return failure(CustomTagError::missing_media, tag);

    if (std::ranges::find(track->tags, tag, &library::TagRecord::id) == track->tags.end())
        return failure(CustomTagError::missing_tag, tag);

    const Snapshot previous = snapshot();
    if (!repository_.remove_tag(media, tag))
        return failure(CustomTagError::missing_tag, tag);

    return commit(previous, tag);
}

CustomTagCommands::Snapshot CustomTagCommands::snapshot() const
{
    return {.tracks = repository_.tracks(),
        .playlists = repository_.playlists(),
        .workspaces = repository_.workspaces()};
}

CustomTagResult CustomTagCommands::commit(const Snapshot& previous, const library::TagId& tag)
{
    if (persist_(repository_))
        return {.error = CustomTagError::none, .tag = tag, .changed = true};

    static_cast<void>(repository_.replace_all(previous.tracks, previous.playlists,
        previous.workspaces));

    return failure(CustomTagError::persistence_failed, tag);
}

} // namespace wiola::app
