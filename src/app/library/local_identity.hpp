/**
 * @file
 * @brief Local-path compatibility for persisted catalog identities.
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

#include <filesystem>

namespace wiola::app {

enum class LocalPathCase {
    native,
    sensitive,
    insensitive,
};

/**
 * Maps a native location to its historical catalog identity without opening the file.
 *
 * Paths are made absolute and lexically normalized, without resolving symlinks. Case-insensitive
 * matching folds ASCII only; non-ASCII bytes remain unchanged, independently of the C locale.
 * This is local-import compatibility policy, not a requirement on catalog or provider identities.
 */
[[nodiscard]] library::MediaId media_id_for(const std::filesystem::path& location,
    LocalPathCase case_policy = LocalPathCase::native);

} // namespace wiola::app
