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

#pragma once

#include <media/byte_stream.hpp>

#include <filesystem>
#include <fstream>
#include <memory>

namespace wiola::media {

class LocalFileStream final : public ByteStream {
public:
    [[nodiscard]] static Result<std::unique_ptr<LocalFileStream>>
    open(const std::filesystem::path& path, const OperationContext& context = {});

    [[nodiscard]] Result<std::size_t> read(std::span<std::byte> out,
        const OperationContext& context = {}) override;
    [[nodiscard]] Result<void> seek(std::int64_t offset, SeekOrigin origin,
        const OperationContext& context = {}) override;
    [[nodiscard]] Result<std::uint64_t> tell(const OperationContext& context = {}) override;

private:
    std::ifstream file_;
};

} // namespace wiola::media
