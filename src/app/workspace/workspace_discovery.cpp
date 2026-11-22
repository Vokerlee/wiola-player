/**
 * @file
 * @brief Workspace discovery values and reconciliation boundaries.
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

#include "workspace_discovery.hpp"
#include <algorithm>
#include <library/fingerprint.hpp>
#include <library/location.hpp>

namespace wiola::app {

bool workspace_contains(const std::filesystem::path& root, const std::filesystem::path& path)
{
    const auto relative = std::filesystem::absolute(path).lexically_normal().lexically_relative(
        std::filesystem::absolute(root).lexically_normal()
    );
    return !relative.empty() && *relative.begin() != "..";
}

WorkspaceDiscovery::WorkspaceDiscovery(const std::filesystem::path& root,
    std::span<const library::TrackRecord> known, std::span<const std::filesystem::path> current)
{
    std::set<std::string> present;
    for (const auto& path : current)
        present.insert(library::media_id_for(path).value);
    for (const auto& record : known) {
        bool within = false, found = false;
        revisions_.emplace(record.id, record.content_revision);
        for (const auto& location : record.locations) {
            const auto key = library::media_id_for(location).value;
            locations_.try_emplace(key, record.id);
            within = within || workspace_contains(root, location);
            found = found || present.contains(key);
        }
        if (within && !found && !record.fingerprint.empty()) {
            const auto [entry, unique] = missing_.try_emplace(record.fingerprint, record.id);
            if (!unique && entry->second != record.id)
                entry->second.reset();
        }
    }
}

WorkspaceScanEntry WorkspaceDiscovery::resolve(const std::filesystem::path& location)
{
    WorkspaceScanEntry result;
    result.location = location;
    result.media = library::media_id_for(location);
    result.fingerprint = library::fingerprint_media(location);
    result.unreadable = !result.fingerprint;
    if (const auto known = locations_.find(result.media.value); known != locations_.end()) {
        result.media = known->second;
        result.kind = WorkspaceDiscoveryKind::known_location;
    } else if (result.fingerprint) {
        const auto missing = missing_.find(*result.fingerprint);
        if (missing != missing_.end() && missing->second &&
            relocated_.insert(*missing->second).second) {
            result.media = *missing->second;
            result.kind = WorkspaceDiscoveryKind::relocation;
        }
    }
    if (result.known())
        result.expected_content_revision = revisions_.at(result.media);
    return result;
}

} // namespace wiola::app
