/**
 * @file
 * @brief Merge learned metadata without replacing user-owned record layers.
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

#include <wiola/library/in_memory_repository.hpp>
#include <wiola/media/metadata.hpp>

namespace wiola::app {

class MetadataMerge final {
public:
    /// Returns true only when a stored embedded field actually changed.
    [[nodiscard]] bool merge(library::InMemoryRepository& repository, const library::MediaId& media,
        const wiola::media::Metadata& learned) const;
};

} // namespace wiola::app
