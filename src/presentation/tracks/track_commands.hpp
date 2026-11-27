/**
 * @file
 * @brief Typed browser commands and context-specific capability values.
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

#include <initializer_list>

namespace wiola::gui {

enum class TrackCommand : unsigned {
    play = 1,
    remove = 2,
    transfer = 4,
    move = 8,
    apply_order = 16,
    append = 64,
    transfer_destination = 128,
};

class TrackCapabilities final {
public:
    constexpr TrackCapabilities() = default;

    constexpr TrackCapabilities(std::initializer_list<TrackCommand> commands)
    {
        for (const auto command : commands)
            commands_ |= static_cast<unsigned>(command);
    }

    [[nodiscard]] constexpr bool supports(TrackCommand command) const noexcept
    {
        return (commands_ & static_cast<unsigned>(command)) != 0;
    }

    static constexpr TrackCapabilities all() noexcept
    {
        return {TrackCommand::play, TrackCommand::remove, TrackCommand::transfer,
            TrackCommand::transfer_destination, TrackCommand::move, TrackCommand::apply_order,
            TrackCommand::append};
    }

    static constexpr TrackCapabilities queue() noexcept
    {
        return {TrackCommand::play, TrackCommand::remove, TrackCommand::transfer,
            TrackCommand::transfer_destination, TrackCommand::apply_order};
    }

    static constexpr TrackCapabilities playlist() noexcept
    {
        return {TrackCommand::play, TrackCommand::transfer, TrackCommand::transfer_destination,
            TrackCommand::move, TrackCommand::apply_order, TrackCommand::append};
    }

    static constexpr TrackCapabilities workspace() noexcept
    {
        return {TrackCommand::play, TrackCommand::transfer, TrackCommand::transfer_destination,
            TrackCommand::apply_order, TrackCommand::append};
    }

    bool operator==(const TrackCapabilities&) const = default;

private:
    unsigned commands_{0};
};

} // namespace wiola::gui
