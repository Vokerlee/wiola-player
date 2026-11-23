/**
 * @file
 * @brief Typed track sort fields, directions, and values.
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

#include <gui/model/tracks/track_sort.hpp>

#include <library/location.hpp>
#include <library/tags.hpp>

namespace wiola::gui {

TrackSortKey TrackSortKey::for_tag(std::string key, TrackSortDirection direction)
{
    auto valid = library::make_tag_value(std::move(key), "value");
    return {.field = TrackSortField::custom_tag,
        .direction = direction,
        .tag_key = valid ? valid->key : std::string{}};
}

bool TrackSortKey::valid() const
{
    if (field == TrackSortField::custom_tag)
        return library::make_tag_value(tag_key, "value").has_value();

    return tag_key.empty() && field >= TrackSortField::title && field <= TrackSortField::path;
}

TrackSortValue track_sort_value(const TrackSortKey& key, const TrackRow& track,
    const library::LibraryRepository* repository)
{
    if (!key.valid())
        return std::monostate{};

    switch (key.field) {
    case TrackSortField::title:
        return library::normalized_tag_text(track.metadata.title);
    case TrackSortField::artist:
        return library::normalized_tag_text(track.metadata.artist);
    case TrackSortField::album:
        return library::normalized_tag_text(track.metadata.album);
    case TrackSortField::duration:
        return track.metadata.duration ? TrackSortValue{*track.metadata.duration}
                                       : TrackSortValue{};
    case TrackSortField::path:
        return track.location_key.empty() ? track.location.generic_string() : track.location_key;
    case TrackSortField::custom_tag:
        break;
    }

    const library::TrackRecord* record = repository ? repository->track(track.media) : nullptr;

    const std::string expected = library::normalized_tag_text(key.tag_key);
    std::string value;

    for (const library::TagRecord& tag : record ? record->tags : track.tags) {
        if (library::normalized_tag_text(tag.key) != expected)
            continue;

        if (!value.empty())
            value += ", ";

        value += library::normalized_tag_text(tag.value);
    }

    return value;
}

} // namespace wiola::gui
