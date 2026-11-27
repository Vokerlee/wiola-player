/**
 * @file
 * @brief Platform file replacement port with bounded recoverable operation records.
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
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace wiola::media {

enum class FileReplacementError {
    none,
    io,
    conflict,
    recovery_required,
    cancelled,
};

enum class FileReplacementStep {
    journal_created,
    before_backup,
    backup_created,
    before_replace,
    replaced,
};

struct FileReplacementRequest {
    std::filesystem::path source, output, backup;
    std::string original_digest, output_digest;
};

struct FileReplacementResult {
    FileReplacementError error{FileReplacementError::none};
    bool source_modified{}, recovered{};
    std::filesystem::path backup, journal;
    std::string detail;
};

class FileReplacement {
public:
    virtual ~FileReplacement() = default;
    virtual FileReplacementResult replace(FileReplacementRequest request,
        const std::function<bool()>& guard = {}) = 0;
    virtual FileReplacementResult recover(const std::filesystem::path& source) = 0;
};

using FileDigest = std::function<std::optional<std::string>(const std::filesystem::path&)>;
using FileReplacementFault = std::function<bool(FileReplacementStep)>;
[[nodiscard]] std::unique_ptr<FileReplacement> native_file_replacement(FileDigest digest,
    FileReplacementFault fault = {});

} // namespace wiola::media
