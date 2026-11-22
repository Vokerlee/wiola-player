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

namespace {

std::filesystem::path normalized(const std::filesystem::path& path)
{
    return std::filesystem::absolute(path).lexically_normal();
}

bool within(const std::filesystem::path& root, const std::filesystem::path& path)
{
    const std::filesystem::path relative = normalized(path).lexically_relative(normalized(root));
    return !relative.empty() && *relative.begin() != "..";
}

} // namespace

WorkspaceReconciliationResult
WorkspaceReconciliation::reconcile(library::InMemoryRepository& repository,
    const library::WorkspaceId& workspace, std::span<const std::filesystem::path> paths) const
{
    const library::WorkspaceRecord* record = repository.workspace(workspace);
    if (record == nullptr)
        return {};

    const std::filesystem::path root = record->root;
    std::set<std::filesystem::path> current;

    for (const std::filesystem::path& path : paths)
        current.insert(normalized(path));

    library::RepositoryTransaction transaction{repository,
        [](const library::LibraryRepository&) { return true; }};
    auto& updated = repository;
    WorkspaceReconciliationResult result{.succeeded = true};

    for (library::TrackRecord track : updated.tracks()) {
        const bool in_workspace = std::ranges::any_of(track.locations,
            [&root](const std::filesystem::path& location) { return within(root, location); });
        const bool present =
            std::ranges::any_of(track.locations, [&current](const std::filesystem::path& location) {
                return current.contains(normalized(location));
            });

        if (in_workspace && !present && !track.missing) {
            ++result.missing;
            track.missing = true;
            static_cast<void>(updated.put_track(std::move(track)));
        }
    }

    for (const std::filesystem::path& path : paths) {
        if (!within(root, path)) {
            ++result.outside;
            continue;
        }

        const std::filesystem::path location = normalized(path);
        const library::MediaId identity = library::media_id_for(location);
        library::TrackRecord track;
        bool relocated = false;

        if (const library::TrackRecord* known = updated.track(identity)) {
            track = *known;
        } else {
            std::vector<library::TrackRecord> candidates;

            for (const library::TrackRecord& candidate : updated.tracks()) {
                if (candidate.missing &&
                    std::ranges::any_of(candidate.locations,
                        [&root](const std::filesystem::path& previous) {
                            return within(root, previous);
                        }))
                    candidates.push_back(candidate);
            }

            const auto match = library::match_relocation(candidates, location);
            if (match) {
                track = *updated.track(*match);
                relocated = true;
            } else {
                track.id = identity;
            }
        }

        const bool was_missing = track.missing;
        const auto change = library::reconcile_location(track, location);

        if (!change) {
            ++result.failed;
            continue;
        }

        if (!updated.put_track(std::move(track)))
            ++result.failed;
        else if (relocated)
            ++result.relocated;
        else if (was_missing)
            ++result.restored;
        else if (change->kind == library::RelocationKind::edited)
            ++result.edited;
        else if (change->kind == library::RelocationKind::learned)
            ++result.learned;
    }

    result.succeeded = transaction.commit();
    return result;
}

} // namespace wiola::app
