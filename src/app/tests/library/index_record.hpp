/**
 * @file
 * @brief Owned catalog records shared by focused index tests.
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

#include <wiola/library/records.hpp>

#include <string>
#include <utility>

namespace wiola::testing {

inline library::TrackRecord index_record(std::string id, std::string title, std::string artist,
    std::string genre)
{
    library::TrackRecord track;
    track.id = library::MediaId{std::move(id)};
    track.embedded.title = std::move(title);
    track.embedded.artist = std::move(artist);
    track.embedded.album = "Album";
    track.tags = {
        {.id = library::TagId{"tag-" + track.id.value}, .key = "Genre", .value = std::move(genre)}
    };

    return track;
}

} // namespace wiola::testing
