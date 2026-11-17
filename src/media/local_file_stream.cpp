/**
 * @file
 * @brief Seekable local-file implementation of ByteStream.
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

#include <media/local_file_stream.hpp>

#include <limits>

namespace wiola::media {

namespace {

Error failure(ErrorCategory category, const char* detail)
{
    return Error{.category = category, .detail = detail};
}

} // namespace

Result<std::unique_ptr<LocalFileStream>> LocalFileStream::open(const std::filesystem::path& path,
    const OperationContext& context)
{
    if (context.stop_reason() != StopReason::none)
        return unexpected{context.error()};

    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error))
        return unexpected{failure(ErrorCategory::unreadable, "local file is not a regular file")};

    auto stream = std::make_unique<LocalFileStream>();
    stream->file_.open(path, std::ios::binary);

    if (!stream->file_)
        return unexpected{failure(ErrorCategory::unreadable, "local file could not be opened")};

    return stream;
}

Result<std::size_t> LocalFileStream::read(std::span<std::byte> out, const OperationContext& context)
{
    if (context.stop_reason() != StopReason::none)
        return unexpected{context.error()};

    if (out.size() > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max()))
        return unexpected{failure(ErrorCategory::read_failed, "read size exceeds stream range")};

    file_.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
    const std::size_t count{static_cast<std::size_t>(file_.gcount())};

    if (file_.bad() || (file_.fail() && !file_.eof()))
        return unexpected{failure(ErrorCategory::read_failed, "local file read failed")};

    return count;
}

Result<void> LocalFileStream::seek(std::int64_t offset, SeekOrigin origin,
    const OperationContext& context)
{
    if (context.stop_reason() != StopReason::none)
        return unexpected{context.error()};

    if (offset < std::numeric_limits<std::streamoff>::min() ||
        offset > std::numeric_limits<std::streamoff>::max())
        return unexpected{failure(ErrorCategory::seek_failed, "seek offset exceeds stream range")};

    file_.clear();
    const std::ios::seekdir direction{origin == SeekOrigin::start ? std::ios::beg
            : origin == SeekOrigin::current                       ? std::ios::cur
                                                                  : std::ios::end};
    file_.seekg(static_cast<std::streamoff>(offset), direction);

    if (!file_)
        return unexpected{failure(ErrorCategory::seek_failed, "local file seek failed")};

    return {};
}

Result<std::uint64_t> LocalFileStream::tell(const OperationContext& context)
{
    if (context.stop_reason() != StopReason::none)
        return unexpected{context.error()};

    file_.clear();
    const std::streampos position{file_.tellg()};

    if (position < 0)
        return unexpected{failure(ErrorCategory::seek_failed, "local file cursor is unavailable")};

    return static_cast<std::uint64_t>(position);
}

} // namespace wiola::media
