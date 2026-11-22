/**
 * @file
 * @brief Incrementally maintained normalized track search fields.
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

#include "metadata_edit_session.hpp"
#include <algorithm>

namespace wiola::app {

MetadataEditSession
MetadataEditSession::capture(const std::vector<const library::TrackRecord*>& records)
{
    MetadataEditSession session;
    std::set<library::MediaId> seen;
    for (const auto* record : records) {
        if (!record || !record->id.valid() || !seen.insert(record->id).second)
            continue;
        session.targets_.push_back({
            record->id, library::resolve_metadata(*record),
            {record->metadata_revision, record->content_revision, record->fingerprint},
            record->overrides != library::StandardFieldOverrides{}
        });
    }
    // Selection order is not identity; selecting another duplicate occurrence keeps the draft.
    std::ranges::sort(session.targets_, {}, &Target::media);
    if (session.targets_.empty())
        return session;
    session.common_ = session.targets_.front().original;
    for (const auto& target : session.targets_) {
        const auto mix = [&](StandardMetadataField field, auto& common, const auto& value) {
            if (common != value) {
                common = {};
                session.mixed_.insert(field);
            }
        };
        mix(StandardMetadataField::title, session.common_.title, target.original.title);
        mix(StandardMetadataField::artist, session.common_.artist, target.original.artist);
        mix(StandardMetadataField::album, session.common_.album, target.original.album);
        mix(StandardMetadataField::genre, session.common_.genre, target.original.genre);
        mix(StandardMetadataField::year, session.common_.year, target.original.year);
        mix(StandardMetadataField::comment, session.common_.comment, target.original.comment);
        mix(StandardMetadataField::composer, session.common_.composer, target.original.composer);
        mix(StandardMetadataField::track_number, session.common_.track_number,
            target.original.track_number);
        mix(StandardMetadataField::disc_number, session.common_.disc_number,
            target.original.disc_number);
    }
    return session;
}

std::vector<library::MediaId> MetadataEditSession::media() const
{
    std::vector<library::MediaId> ids;
    for (const auto& target : targets_)
        ids.push_back(target.media);
    return ids;
}

std::map<library::MediaId, ExpectedMediaState> MetadataEditSession::expectations() const
{
    std::map<library::MediaId, ExpectedMediaState> expected;
    for (const auto& target : targets_)
        expected.emplace(target.media, target.expected);
    return expected;
}

bool MetadataEditSession::overrides_present() const
{
    return std::ranges::any_of(targets_, &Target::overrides_present);
}

} // namespace wiola::app
