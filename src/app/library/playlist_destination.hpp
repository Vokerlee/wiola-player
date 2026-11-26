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

#pragma once

#include <wiola/library/identity.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <utility>

namespace wiola::app {

class PlaylistDestination final {
public:
    PlaylistDestination() = default;
    PlaylistDestination(const PlaylistDestination&) = delete;
    PlaylistDestination& operator=(const PlaylistDestination&) = delete;
    PlaylistDestination(PlaylistDestination&&) = delete;
    PlaylistDestination& operator=(PlaylistDestination&&) = delete;

    [[nodiscard]] bool set(const library::PlaylistId& id);
    [[nodiscard]] bool clear_if(const library::PlaylistId& id);
    [[nodiscard]] const std::optional<library::PlaylistId>& id() const noexcept;

    using Observer = std::function<void(std::optional<library::PlaylistId>)>;

    void on_changed(Observer observer);

private:
    std::optional<library::PlaylistId> id_;
    std::shared_ptr<Observer> observer_;
};

} // namespace wiola::app
