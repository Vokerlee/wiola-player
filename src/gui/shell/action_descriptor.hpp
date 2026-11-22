/**
 * @file
 * @brief Shared actions for every existing player command.
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
#include <QKeySequence>
#include <QStyle>
#include <optional>
#include <span>

namespace wiola::gui {

enum class PlayerAction : std::size_t {
    open,
    previous,
    play,
    next,
    stop,
    repeat,
    shuffle,
    add,
    remove,
    clear,
    equalizer,
    count,
};

struct ActionDescriptor {
    PlayerAction id;
    const char* key;
    const char* label;
    const char* shortcut;
    std::optional<QStyle::StandardPixmap> icon;
    bool checkable{};
};

[[nodiscard]] std::span<const ActionDescriptor> action_descriptors();
[[nodiscard]] const ActionDescriptor* action_descriptor(PlayerAction id);
[[nodiscard]] std::optional<PlayerAction> parse_action(const QString& key);
[[nodiscard]] QString action_name(PlayerAction id);

} // namespace wiola::gui
