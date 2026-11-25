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
#include <wiola/app/library/local_identity.hpp>

namespace wiola::app {

bool workspace_contains(const std::filesystem::path& root, const std::filesystem::path& path)
{
    std::error_code error;
    const auto absolute = std::filesystem::absolute(path, error).lexically_normal();
    if (error)
        return false;
    const auto parent = std::filesystem::absolute(root, error).lexically_normal();
    if (error)
        return false;
    const auto relative = absolute.lexically_relative(parent);
    return !relative.empty() && *relative.begin() != "..";
}

WorkspaceDiscovery::WorkspaceDiscovery(WorkspaceFiles& files, const std::filesystem::path& root,
    std::span<const library::TrackRecord> known, std::span<const std::filesystem::path> current)
    : files_{files}
{
    present_.emplace();
    for (const auto& path : current)
        present_->insert(app::media_id_for(path).value);
    index(root, known);
}

WorkspaceDiscovery::WorkspaceDiscovery(WorkspaceFiles& files, const std::filesystem::path& root,
    std::span<const library::TrackRecord> known, std::stop_token stop)
    : files_{files}
    , stop_{stop}
{
    index(root, known);
}

void WorkspaceDiscovery::index(const std::filesystem::path& root,
    std::span<const library::TrackRecord> known)
{
    for (const auto& record : known) {
        bool within = false;
        revisions_.emplace(record.id, record.content_revision);
        for (const auto& location : record.locations) {
            locations_.try_emplace(app::media_id_for(location).value, record.id);
            within = within || workspace_contains(root, location);
        }
        if (within && !record.fingerprint.empty())
            candidates_[record.fingerprint].push_back({record.id, record.locations});
    }
}

WorkspaceScanEntry WorkspaceDiscovery::resolve(const std::filesystem::path& location)
{
    WorkspaceScanEntry result;
    result.location = location;
    result.media = app::media_id_for(location);
    result.fingerprint = files_.fingerprint(location, stop_);
    result.unreadable = !result.fingerprint;
    if (const auto known = locations_.find(result.media.value); known != locations_.end()) {
        result.media = known->second;
        result.kind = WorkspaceDiscoveryKind::known_location;
    } else if (result.fingerprint) {
        if (const auto candidates = candidates_.find(*result.fingerprint);
            candidates != candidates_.end()) {
            std::optional<library::MediaId> unique;
            bool ambiguous = false;
            for (const auto& candidate : candidates->second) {
                if (relocated_.contains(candidate.media))
                    continue;
                bool present = false;
                for (const auto& path : candidate.locations) {
                    if (present_)
                        present = present || present_->contains(app::media_id_for(path).value);
                    else {
                        present =
                            present || files_.presence(path) != WorkspaceFiles::Presence::absent;
                    }
                }
                if (present)
                    continue;
                if (unique && *unique != candidate.media) {
                    ambiguous = true;
                    break;
                }
                unique = candidate.media;
            }
            if (unique && !ambiguous) {
                result.media = *unique;
                result.kind = WorkspaceDiscoveryKind::relocation;
                relocated_.insert(*unique);
            }
        }
    }
    if (result.known())
        result.expected_content_revision = revisions_.at(result.media);
    return result;
}

} // namespace wiola::app
