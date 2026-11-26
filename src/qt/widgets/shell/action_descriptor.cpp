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

#include "action_descriptor.hpp"
#include <algorithm>
#include <array>

namespace wiola::gui {

std::span<const ActionDescriptor> action_descriptors()
{
    static const std::array descriptors{
        ActionDescriptor{PlayerAction::open, "open", "Open tracks", "Ctrl+O",
                         QStyle::SP_DirOpenIcon},
        ActionDescriptor{PlayerAction::previous, "previous", "Previous track", "Ctrl+Left",
                         QStyle::SP_MediaSkipBackward},
        ActionDescriptor{PlayerAction::play, "play", "Play", "Space", QStyle::SP_MediaPlay},
        ActionDescriptor{PlayerAction::next, "next", "Next track", "Ctrl+Right",
                         QStyle::SP_MediaSkipForward},
        ActionDescriptor{PlayerAction::stop, "stop", "Stop", "Escape", QStyle::SP_MediaStop},
        ActionDescriptor{PlayerAction::repeat, "repeat", "Repeat: off", "Ctrl+R", std::nullopt},
        ActionDescriptor{PlayerAction::shuffle, "shuffle", "Shuffle", "Ctrl+Shift+S", std::nullopt,
                         true},
        ActionDescriptor{PlayerAction::add, "add", "Add", "Ctrl+Shift+A", std::nullopt},
        ActionDescriptor{PlayerAction::remove, "remove", "Remove", "Delete", std::nullopt},
        ActionDescriptor{PlayerAction::clear, "clear", "Clear play queue", "Ctrl+Shift+Backspace",
                         std::nullopt},
        ActionDescriptor{PlayerAction::equalizer, "equalizer", "EQ", "Ctrl+E", std::nullopt}
    };
    return descriptors;
}

const ActionDescriptor* action_descriptor(PlayerAction id)
{
    for (const auto& descriptor : action_descriptors())
        if (descriptor.id == id)
            return &descriptor;
    return nullptr;
}

std::optional<PlayerAction> parse_action(const QString& key)
{
    for (const auto& descriptor : action_descriptors())
        if (key == descriptor.key)
            return descriptor.id;
    return {};
}

QString action_name(PlayerAction id)
{
    if (const auto* descriptor = action_descriptor(id))
        return QString::fromLatin1(descriptor->key);
    return {};
}

} // namespace wiola::gui
