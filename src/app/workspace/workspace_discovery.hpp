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

#pragma once
#include <map>
#include <set>
#include <span>
#include <stop_token>
#include <wiola/library/records.hpp>

namespace wiola::app {

enum class WorkspaceDiscoveryKind {
    new_media,
    known_location,
    relocation,
};

struct WorkspaceScanEntry {
    library::MediaId media;
    std::filesystem::path location;
    std::optional<std::string> fingerprint;
    std::uint64_t expected_content_revision{0};
    WorkspaceDiscoveryKind kind{WorkspaceDiscoveryKind::new_media};

    [[nodiscard]] bool known() const noexcept { return kind != WorkspaceDiscoveryKind::new_media; }

    [[nodiscard]] bool relocated() const noexcept
    {
        return kind == WorkspaceDiscoveryKind::relocation;
    }

    bool unreadable{false};
};

[[nodiscard]] bool workspace_contains(const std::filesystem::path& root,
    const std::filesystem::path& path);

/// Worker-owned resolver over an immutable library snapshot. Fingerprinting never runs in a view.
class WorkspaceDiscovery final {
public:
    WorkspaceDiscovery(const std::filesystem::path& root,
        std::span<const library::TrackRecord> known,
        std::span<const std::filesystem::path> current);
    WorkspaceDiscovery(const std::filesystem::path& root,
        std::span<const library::TrackRecord> known, std::stop_token stop = {});
    [[nodiscard]] WorkspaceScanEntry resolve(const std::filesystem::path& location);

private:
    void index(const std::filesystem::path& root, std::span<const library::TrackRecord> known);

    struct Candidate {
        library::MediaId media;
        std::vector<std::filesystem::path> locations;
    };

    std::optional<std::set<std::string>> present_;
    std::map<std::string, std::vector<Candidate>> candidates_;
    std::stop_token stop_;
    std::map<std::string, library::MediaId> locations_;
    std::map<library::MediaId, std::uint64_t> revisions_;
    std::set<library::MediaId> relocated_;
};

} // namespace wiola::app
