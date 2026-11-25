/**
 * @file
 * @brief Native file observations for the directory scanner.
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

#include "native_workspace_files.hpp"

#include <media/local/fingerprint.hpp>

namespace wiola::app {

std::optional<std::string> NativeWorkspaceFiles::fingerprint(const std::filesystem::path& path,
    std::stop_token stopped)
{
    return media::fingerprint_media_cancellable(path, stopped);
}

WorkspaceFiles::Presence NativeWorkspaceFiles::presence(const std::filesystem::path& path)
{
    std::error_code error;
    const bool exists = std::filesystem::exists(path, error);
    if (error)
        return Presence::unknown;
    return exists ? Presence::present : Presence::absent;
}

} // namespace wiola::app
