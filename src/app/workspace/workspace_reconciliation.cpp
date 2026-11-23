/**
 * @file
 * @brief Atomic reconciliation of workspace files with persistent media records.
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

#include "workspace_reconciliation.hpp"

#include <library/fingerprint.hpp>
#include <library/location.hpp>
#include <library/repository_transaction.hpp>

#include <algorithm>
#include <set>

namespace wiola::app {

WorkspaceReconciliationResult
WorkspaceReconciliation::reconcile(library::InMemoryRepository& repository,
    const library::WorkspaceId& workspace, std::span<const WorkspaceScanEntry> entries) const
{
    const auto* definition = repository.workspace(workspace);
    if (!definition)
        return {};
    const auto root = definition->root;
    const auto in_scope = [root, recursive = definition->recursive](const auto& path) {
        return workspace_contains(root, path) &&
            (recursive || library::media_id_for(path.parent_path()) == library::media_id_for(root));
    };
    library::RepositoryTransaction transaction{repository, persist_};
    WorkspaceReconciliationResult result;
    std::set<library::MediaId> seen;
    for (const auto& entry : entries) {
        if (!in_scope(entry.location)) {
            ++result.outside;
            continue;
        }
        if (!entry.media.valid()) {
            ++result.failed;
            continue;
        }
        seen.insert(entry.media);
        const auto* current = repository.track(entry.media);
        if ((entry.known() && !current) ||
            (current && current->content_revision != entry.expected_content_revision)) {
            ++result.failed;
            continue;
        }
        library::TrackRecord track;
        if (current)
            track = *current;
        else
            track.id = entry.media;
        const bool was_missing = track.missing;
        const bool relocated = entry.relocated();
        if (entry.fingerprint) {
            const auto change =
                library::reconcile_location(track, entry.location, *entry.fingerprint);
            if (!change) {
                ++result.failed;
                continue;
            }
            if (relocated)
                ++result.relocated;
            else if (was_missing)
                ++result.restored;
            else if (change->kind == library::RelocationKind::edited)
                ++result.edited;
            else if (change->kind == library::RelocationKind::learned)
                ++result.learned;
        } else {
            if (std::ranges::find(track.locations, entry.location) == track.locations.end())
                track.locations.push_back(entry.location);
            track.missing = entry.unreadable;
            if (entry.unreadable)
                ++result.failed;
        }
        if (entry.relocated()) {
            const auto preferred = std::ranges::find(track.locations, entry.location);
            if (preferred != track.locations.end())
                std::rotate(track.locations.begin(), preferred, preferred + 1);
        }
        if (!repository.put_track(std::move(track)))
            ++result.failed;
    }
    for (auto track : repository.tracks()) {
        if (seen.contains(track.id) || track.missing)
            continue;
        if (!track.locations.empty() && std::ranges::all_of(track.locations, [&](const auto& path) {
                return in_scope(path);
            })) {
            track.missing = true;
            if (repository.put_track(std::move(track)))
                ++result.missing;
        }
    }
    result.succeeded = transaction.commit();
    return result;
}

} // namespace wiola::app
