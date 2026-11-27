/**
 * @file
 * @brief Expected-state checks before replacing metadata or file data.
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

#include "metadata_conflicts.hpp"

#include <utility>

namespace wiola::app {

MetadataConflictDetector::MetadataConflictDetector(ReadFingerprint read)
    : read_{std::move(read)}
{
}

MetadataConflictKind MetadataConflictDetector::check(const library::LibraryRepository& repository,
    const library::MediaId& media, const ExpectedMediaState& expected,
    const std::filesystem::path& location) const
{
    const library::TrackRecord* track = repository.track(media);

    if (track == nullptr)
        return MetadataConflictKind::missing_media;

    if (track->metadata_revision != expected.metadata_revision)
        return MetadataConflictKind::metadata_changed;

    if (track->content_revision != expected.content_revision)
        return MetadataConflictKind::content_changed;

    if (expected.fingerprint.empty())
        return MetadataConflictKind::none;

    const std::filesystem::path candidate =
        location.empty() && !track->locations.empty() ? track->locations.front() : location;

    if (candidate.empty() || !read_)
        return MetadataConflictKind::unreadable;

    try {
        const auto fingerprint = read_(candidate);

        if (!fingerprint)
            return MetadataConflictKind::unreadable;

        return *fingerprint == expected.fingerprint ? MetadataConflictKind::none
                                                    : MetadataConflictKind::content_changed;
    } catch (...) {
        return MetadataConflictKind::unreadable;
    }
}

} // namespace wiola::app
