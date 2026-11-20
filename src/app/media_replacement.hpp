/**
 * @file
 * @brief Validated atomic replacement of local media after a metadata write.
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

#include <media/media.hpp>
#include <media/operation_context.hpp>
#include <media/provider.hpp>

#include <filesystem>
#include <string_view>

namespace wiola::app {

/// Reopens and compares source/output audio, retains a backup, then atomically renames the
/// validated output over the source. Validation or conflict failures never replace the source.
[[nodiscard]] media::MetadataWriteOutcome
commit_media_replacement(const std::filesystem::path& source,
    const std::filesystem::path& temporary, media::MediaProvider& provider,
    std::string_view expected_fingerprint = {}, const media::OperationContext& context = {});

} // namespace wiola::app
