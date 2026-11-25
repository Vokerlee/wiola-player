/**
 * @file
 * @brief File observations required by workspace discovery policy.
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

#include <filesystem>
#include <optional>
#include <stop_token>
#include <string>

namespace wiola::app {

/// Supplied observations; unknown presence must never be treated as evidence of absence.
class WorkspaceFiles {
public:
    enum class Presence {
        absent,
        present,
        unknown,
    };
    virtual ~WorkspaceFiles() = default;
    [[nodiscard]] virtual std::optional<std::string> fingerprint(const std::filesystem::path& path,
        std::stop_token stopped) = 0;
    [[nodiscard]] virtual Presence presence(const std::filesystem::path& path) = 0;
};

} // namespace wiola::app
