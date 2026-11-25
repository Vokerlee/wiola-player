/**
 * @file
 * @brief Captured metadata edit targets, expected state, mixed fields and draft.
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
#include <map>
#include <set>
#include <wiola/library/metadata.hpp>
#include <wiola/library/metadata_patch.hpp>

namespace wiola::app {

enum class StandardMetadataField {
    title,
    artist,
    album,
    genre,
    year,
    comment,
    composer,
    track_number,
    disc_number,
};

/// Immutable captured targets and expectations; only the explicit patch draft can change.
class MetadataEditSession final {
public:
    struct Target {
        library::MediaId media;
        library::ResolvedMetadata original;
        ExpectedMediaState expected;
        bool overrides_present{false};
        bool operator==(const Target&) const = default;
    };

    [[nodiscard]] static MetadataEditSession
    capture(const std::vector<const library::TrackRecord*>& records);

    [[nodiscard]] const std::vector<Target>& targets() const { return targets_; }

    [[nodiscard]] std::vector<library::MediaId> media() const;
    [[nodiscard]] std::map<library::MediaId, ExpectedMediaState> expectations() const;

    [[nodiscard]] const library::ResolvedMetadata& common() const { return common_; }

    [[nodiscard]] bool mixed(StandardMetadataField field) const { return mixed_.contains(field); }

    [[nodiscard]] bool valid() const { return !targets_.empty(); }

    [[nodiscard]] bool overrides_present() const;

    void set_draft(library::StandardMetadataPatch draft) { draft_ = std::move(draft); }

    [[nodiscard]] const library::StandardMetadataPatch& draft() const { return draft_; }

    [[nodiscard]] bool same_targets(const MetadataEditSession& other) const
    {
        return targets_ == other.targets_;
    }

private:
    std::vector<Target> targets_;
    library::ResolvedMetadata common_;
    std::set<StandardMetadataField> mixed_;
    library::StandardMetadataPatch draft_;
};

} // namespace wiola::app
