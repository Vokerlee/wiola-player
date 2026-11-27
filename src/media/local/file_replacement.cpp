/**
 * @file
 * @brief Native replacement primitives and verified recovery without deleting the only source copy.
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

#include "file_replacement.hpp"

#include <fstream>
#include <iomanip>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif
namespace wiola::media {
namespace {

std::string utf8(const std::filesystem::path& path)
{
    const auto text = path.u8string();
    return {text.begin(), text.end()};
}

std::filesystem::path journal_for(std::filesystem::path source)
{
    source += ".wiola-replacement";
    return source;
}

bool sync_file(const std::filesystem::path& path)
{
#ifdef _WIN32
    const auto file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    const bool ok = FlushFileBuffers(file) != 0;
    CloseHandle(file);
    return ok;
#else
    const int file = ::open(path.c_str(), O_RDONLY);
    if (file < 0)
        return false;
    const bool ok = ::fsync(file) == 0;
    ::close(file);
    return ok;
#endif
}

class NativeReplacement final : public FileReplacement {
public:
    NativeReplacement(FileDigest digest, FileReplacementFault fault)
        : digest_{std::move(digest)}
        , fault_{std::move(fault)}
    {
        if (!digest_)
            throw std::invalid_argument{"file replacement requires a digest function"};
    }

    FileReplacementResult replace(FileReplacementRequest request,
        const std::function<bool()>& guard) override;
    FileReplacementResult recover(const std::filesystem::path& source) override;

private:
    bool boundary(FileReplacementStep step) const { return !fault_ || fault_(step); }

    bool matches(const std::filesystem::path& file, const std::string& digest) const
    {
        return digest_(file) == std::optional{digest};
    }

    FileDigest digest_;
    FileReplacementFault fault_;
};

bool normalized(FileReplacementRequest& request)
{
    std::error_code error;

    for (auto* path : {&request.source, &request.output, &request.backup}) {
        *path = std::filesystem::absolute(*path, error).lexically_normal();
        if (error)
            return false;
    }

    return request.source != request.output && request.source != request.backup &&
        request.output != request.backup &&
        request.source.parent_path() == request.output.parent_path() &&
        request.source.parent_path() == request.backup.parent_path() &&
        !request.original_digest.empty() && !request.output_digest.empty();
}

bool record(const std::filesystem::path& journal, const FileReplacementRequest& request)
{
    {
        std::ofstream file{journal, std::ios::binary | std::ios::trunc};
        file << "wiola-replacement 1\n"
             << std::quoted(utf8(request.source)) << '\n'
             << std::quoted(utf8(request.output)) << '\n'
             << std::quoted(utf8(request.backup)) << '\n'
             << std::quoted(request.original_digest) << '\n'
             << std::quoted(request.output_digest) << '\n';
        file.flush();

        if (!file)
            return false;
    }
    return sync_file(journal);
}

std::optional<FileReplacementRequest> read_record(const std::filesystem::path& journal)
{
    std::error_code error;
    const auto size = std::filesystem::file_size(journal, error);

    if (error || size > 64 * 1024)
        return {};

    std::ifstream file{journal, std::ios::binary};
    std::string magic, source, output, backup;
    int version{};
    FileReplacementRequest request;

    if (!(file >> magic >> version) || magic != "wiola-replacement" || version != 1)
        return {};

    if (!(file >> std::quoted(source) >> std::quoted(output) >> std::quoted(backup) >>
            std::quoted(request.original_digest) >> std::quoted(request.output_digest)))
        return {};

    file >> std::ws;

    if (!file.eof())
        return {};

    request.source = std::filesystem::u8path(source);
    request.output = std::filesystem::u8path(output);
    request.backup = std::filesystem::u8path(backup);

    if (!normalized(request))
        return {};

    return request;
}

FileReplacementResult NativeReplacement::recover(const std::filesystem::path& source)
{
    std::error_code error;
    const auto path = std::filesystem::absolute(source, error).lexically_normal();
    FileReplacementResult result;
    result.journal = journal_for(path);

    if (error) {
        result.error = FileReplacementError::io;
        result.detail = error.message();

        return result;
    }

    if (!std::filesystem::exists(result.journal, error)) {
        if (error) {
            result.error = FileReplacementError::io;
            result.detail = error.message();
        }
        return result;
    }

    const auto record = read_record(result.journal);

    if (!record || record->source != path) {
        result.error = FileReplacementError::recovery_required;
        result.detail = "Invalid replacement record; all files retained";

        return result;
    }

    result.backup = record->backup;

    const bool old = matches(path, record->original_digest),
               updated = matches(path, record->output_digest);

    if (!old && !updated) {
        if (std::filesystem::exists(path, error) || error ||
            !matches(record->backup, record->original_digest)) {
            result.error = FileReplacementError::recovery_required;
            result.detail = "Source needs review; original backup and operation record retained";

            return result;
        }

        std::filesystem::copy_file(record->backup, path, std::filesystem::copy_options::none,
            error);

        if (error || !matches(path, record->original_digest)) {
            result.error = FileReplacementError::recovery_required;
            result.detail = "Original remains in backup; restore did not complete";

            return result;
        }
    }

    // Remove only the expected abandoned staged copy, after a known source is available.
    if (matches(record->output, record->output_digest))
        std::filesystem::remove(record->output, error);

    if (error) {
        result.error = FileReplacementError::io;
        result.detail = error.message();

        return result;
    }

    std::filesystem::remove(result.journal, error);
    result.error = error ? FileReplacementError::io : FileReplacementError::none;
    result.recovered = !error;
    result.source_modified = updated;

    return result;
}

FileReplacementResult NativeReplacement::replace(FileReplacementRequest request,
    const std::function<bool()>& guard)
{
    FileReplacementResult result;

    if (!normalized(request)) {
        result.error = FileReplacementError::conflict;
        result.detail = "replacement paths or digests are invalid";

        return result;
    }

    result.backup = request.backup;
    result.journal = journal_for(request.source);

    std::error_code error;
    const auto fail = [&](FileReplacementError kind, std::string detail) {
        result.error = kind;
        result.detail = std::move(detail);

        return result;
    };

    if (std::filesystem::exists(result.journal, error) || error)
        return fail(FileReplacementError::recovery_required,
            "An unfinished replacement record exists; recover before retrying");

    if (std::filesystem::exists(request.backup, error) || error)
        return fail(FileReplacementError::recovery_required,
            "A previous backup already exists; it was retained");

    if (!matches(request.source, request.original_digest) ||
        !matches(request.output, request.output_digest))
        return fail(FileReplacementError::conflict, "source or staged output changed");

    if (!sync_file(request.output))
        return fail(FileReplacementError::io, "could not flush staged output");

    if (!record(result.journal, request))
        return fail(FileReplacementError::io, "could not flush replacement record");

    if (!boundary(FileReplacementStep::journal_created) ||
        !boundary(FileReplacementStep::before_backup))
        return fail(FileReplacementError::io, "replacement interrupted before backup");
#ifndef _WIN32
    const auto permissions = std::filesystem::status(request.source, error).permissions();
    if (error)
        return fail(FileReplacementError::io, error.message());
    std::filesystem::permissions(request.output, permissions,
        std::filesystem::perm_options::replace, error);
    if (error)
        return fail(FileReplacementError::io, error.message());
    std::filesystem::copy_file(request.source, request.backup, std::filesystem::copy_options::none,
        error);
    if (error || !matches(request.backup, request.original_digest) || !sync_file(request.backup))
        return fail(FileReplacementError::io, "could not preserve the original backup");
#endif
#ifndef _WIN32
    if (!boundary(FileReplacementStep::backup_created))
        return fail(FileReplacementError::io, "replacement interrupted after backup");
#endif
    if (!boundary(FileReplacementStep::before_replace))
        return fail(FileReplacementError::io, "replacement interrupted before commit");

    if (guard && !guard())
        return fail(FileReplacementError::cancelled, "replacement was cancelled or refused");

    // Validate again after the potentially lengthy backup and immediately before the native commit.
    if (!matches(request.source, request.original_digest) ||
        !matches(request.output, request.output_digest))
        return fail(FileReplacementError::conflict, "source or output changed at commit");
#ifdef _WIN32
    if (!ReplaceFileW(request.source.c_str(), request.output.c_str(), request.backup.c_str(), 0,
            nullptr, nullptr)) {
        const auto native_error = GetLastError();
        result.detail = std::system_category().message(static_cast<int>(native_error));
        result.error = FileReplacementError::io;

        // Some native errors leave the original under the backup name. Recover by verified copy.
        const auto recovered = recover(request.source);
        result.recovered = recovered.recovered;

        if (recovered.error != FileReplacementError::none) {
            result.error = FileReplacementError::recovery_required;
            result.detail += "; original/recovery record retained";
        }

        return result;
    }
#else
    std::filesystem::rename(request.output, request.source, error);
    if (error)
        return fail(FileReplacementError::io, error.message());
#endif
    result.source_modified = true;
#ifdef _WIN32
    if (!boundary(FileReplacementStep::backup_created))
        return fail(FileReplacementError::io,
            "replacement committed; backup and operation record retained");
#endif
    if (!boundary(FileReplacementStep::replaced))
        return fail(FileReplacementError::io,
            "replacement committed; operation record retained for recovery");

    std::filesystem::remove(result.journal, error);

    if (error)
        return fail(FileReplacementError::io, "file replaced; recovery record cleanup failed");

    return result;
}

} // namespace

std::unique_ptr<FileReplacement> native_file_replacement(FileDigest digest,
    FileReplacementFault fault)
{
    return std::make_unique<NativeReplacement>(std::move(digest), std::move(fault));
}

} // namespace wiola::media
