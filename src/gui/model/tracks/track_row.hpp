/**
 * @file
 * @brief Transport-independent track presentation values and explicit library resolution.
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
#include <gui/model/tracks/track_identity.hpp>
#include <library/records.hpp>
#include <memory>
#include <wiola/media/artwork_reference.hpp>
#include <wiola/media/metadata.hpp>

namespace wiola::gui {

struct TrackRow {
    TrackRowId id;
    library::MediaId media;
    std::filesystem::path location;
    std::string location_key;
    media::TrackMetadata metadata;
    std::vector<library::TagRecord> tags;
    bool missing{false};
    std::uint64_t metadata_revision{0};
    std::uint64_t content_revision{0};
    media::ArtworkReference artwork;
    bool operator==(const TrackRow&) const = default;
};

[[nodiscard]] TrackRow present_record(TrackRowId occurrence, const library::TrackRecord& record);

} // namespace wiola::gui
