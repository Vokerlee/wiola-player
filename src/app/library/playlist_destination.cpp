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

#include "playlist_destination.hpp"
#include <utility>

namespace wiola::app {

bool PlaylistDestination::set(const library::PlaylistId& id)
{
    if (!id.valid())
        return false;

    if (id_ == id)
        return true;
    auto next = std::optional{id};
    const auto notify = observer_;
    auto event = notify ? next : std::optional<library::PlaylistId>{};
    id_.swap(next);
    if (notify)
        (*notify)(std::move(event));
    return true;
}

bool PlaylistDestination::clear_if(const library::PlaylistId& id)
{
    if (id_ != id)
        return false;

    const auto notify = observer_;
    id_.reset();
    if (notify)
        (*notify)({});
    return true;
}

void PlaylistDestination::on_changed(Observer observer)
{
    auto next = observer ? std::make_shared<Observer>(std::move(observer)) : nullptr;
    auto previous = std::exchange(observer_, std::move(next));
}

const std::optional<library::PlaylistId>& PlaylistDestination::id() const noexcept
{
    return id_;
}

} // namespace wiola::app
