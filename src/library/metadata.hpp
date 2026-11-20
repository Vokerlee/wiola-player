/**
 * @file
 * @brief Explicit resolution of embedded, overridden, and user-owned metadata layers.
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

#include <library/records.hpp>

namespace wiola::library {

struct ResolvedMetadata {
    std::string title;
    std::string artist;
    std::string album;
    std::optional<units::Time> duration;
    std::vector<TagRecord> tags;
    bool missing{false};
};

[[nodiscard]] ResolvedMetadata resolve_metadata(const TrackRecord& track);

} // namespace wiola::library
