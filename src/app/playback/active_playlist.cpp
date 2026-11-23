/**
 * @file
 * @brief Active destination playlist identity, independent of visible documents.
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

#include "active_playlist.hpp"

namespace wiola::app {

bool ActivePlaylist::set(const library::PlaylistId& id)
{
    if (!id.valid())
        return false;

    if (id_ == id)
        return true;
    id_ = id;
    if (observer_) {
        const auto notify = observer_;
        notify(id_);
    }
    return true;
}

bool ActivePlaylist::clear_if(const library::PlaylistId& id)
{
    if (id_ != id)
        return false;

    id_.reset();
    if (observer_) {
        const auto notify = observer_;
        notify(id_);
    }
    return true;
}

const std::optional<library::PlaylistId>& ActivePlaylist::id() const noexcept
{
    return id_;
}

} // namespace wiola::app
