/**
 * @file
 * @brief Private FFmpeg input ownership over an independent ByteStream.
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

#include <codec/decode/decoder.hpp>
#include <media/byte_stream.hpp>
#include <media/media.hpp>

#include <cstdint>
#include <memory>
#include <optional>

struct AVFormatContext;
struct AVIOContext;
struct AVIOInterruptCB;
struct AVDictionary;

namespace wiola::codec::ffmpeg {

struct FfmpegStreamFacts {
    std::size_t stream_index;
    DecoderStreamInfo output;
};

using StreamInfoReader = int (*)(AVFormatContext*, AVDictionary**);

class FfmpegInput final {
public:
    [[nodiscard]] static media::Result<std::unique_ptr<FfmpegInput>>
    open(std::unique_ptr<media::ByteStream> stream, const media::OperationContext& context = {});

    ~FfmpegInput();

    FfmpegInput(const FfmpegInput&) = delete;
    FfmpegInput& operator=(const FfmpegInput&) = delete;
    FfmpegInput(FfmpegInput&&) = delete;
    FfmpegInput& operator=(FfmpegInput&&) = delete;

    [[nodiscard]] AVFormatContext* format() const noexcept { return format_; }

    [[nodiscard]] const media::OperationContext& context() const noexcept { return context_; }

    [[nodiscard]] media::Result<FfmpegStreamFacts>
    inspect(StreamInfoReader read_stream_info = nullptr);

    [[nodiscard]] const std::optional<media::Error>& io_error() const noexcept { return io_error_; }

private:
    explicit FfmpegInput(std::unique_ptr<media::ByteStream> stream,
        const media::OperationContext& context) noexcept;

    [[nodiscard]] media::Result<void> initialize();
    [[nodiscard]] media::Result<void> measure_size();

    static int read(void* opaque, unsigned char* output, int size) noexcept;
    static std::int64_t seek(void* opaque, std::int64_t offset, int origin) noexcept;
    static int interrupted(void* opaque) noexcept;
    static int reject_secondary_open(AVFormatContext*, AVIOContext**, const char*, int,
        AVDictionary**) noexcept;

    int read(unsigned char* output, int size) noexcept;
    std::int64_t seek(std::int64_t offset, int origin) noexcept;

    std::unique_ptr<media::ByteStream> stream_;
    media::OperationContext context_;
    AVFormatContext* format_{nullptr};
    void* io_{nullptr};
    std::uint64_t size_{0};
    std::optional<media::Error> io_error_;
};

} // namespace wiola::codec::ffmpeg
