/**
 * @file
 * @brief Registration and state tests for shared player actions.
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

#include <qt/widgets/shell/player_dialogs.hpp>

#include <functional>

namespace wiola::testing {

/// Prompts are explicit gates: tests can change focus or destroy a receiver before returning.
class ScriptedPlayerDialogs final : public gui::PlayerDialogs {
public:
    std::function<void()> prompted;
    std::vector<std::filesystem::path> paths;
    std::optional<std::filesystem::path> chosen_path;
    std::optional<library::PlaylistId> chosen_destination;
    int prompts{};

    void prompt()
    {
        ++prompts;

        const auto hook = prompted;

        if (hook)
            hook();
    }

    std::vector<std::filesystem::path> tracks(std::span<const std::string>) override
    {
        prompt();
        return paths;
    }

    std::optional<std::filesystem::path> folder() override
    {
        prompt();
        return chosen_path;
    }

    std::optional<std::filesystem::path> file(gui::PlayerFileChoice) override
    {
        prompt();
        return chosen_path;
    }

    std::optional<library::PlaylistId> destination(std::span<const gui::PlaylistChoice>) override
    {
        prompt();
        return chosen_destination;
    }
};

} // namespace wiola::testing
