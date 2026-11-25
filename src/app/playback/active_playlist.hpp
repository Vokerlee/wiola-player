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
#include <optional>

namespace wiola::app {

class ActivePlaylist final {
public:
    [[nodiscard]] bool set(const library::PlaylistId& id);
    [[nodiscard]] bool clear_if(const library::PlaylistId& id);
    [[nodiscard]] const std::optional<library::PlaylistId>& id() const noexcept;

    using Observer = std::function<void(std::optional<library::PlaylistId>)>;

    void on_changed(Observer observer) { observer_ = std::move(observer); }

private:
    Observer observer_;
    std::optional<library::PlaylistId> id_;
};

} // namespace wiola::app
