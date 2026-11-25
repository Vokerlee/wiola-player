/**
 * @file
 * @brief Local file resource composition and native path conversion.
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

#include <wiola/media/resource.hpp>
#include <wiola/media/result.hpp>

#include <filesystem>
#include <span>
#include <vector>

namespace wiola::media {

/// Conversion only: no probing, canonicalization or file opening. Local keys use generic UTF-8
/// path spelling; namespace ownership is checked before reversing a reference into a file path.
[[nodiscard]] ResourceReference local_resource(const std::filesystem::path& path);
[[nodiscard]] std::vector<ResourceReference>
local_resources(std::span<const std::filesystem::path> paths);
[[nodiscard]] Result<std::filesystem::path> local_path(const ResourceReference& resource);

} // namespace wiola::media
