/**
 * @file
 * @brief Stable queue identity, immutable location, and mutable track metadata.
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

#include <queue/track.hpp>

#include <utility>

namespace wiola::engine {

Track Track::of(std::filesystem::path path)
{
    Track track;
    track.path_ = std::move(path);
    track.metadata_.title = track.path_.stem().string();
    return track;
}

} // namespace wiola::engine
