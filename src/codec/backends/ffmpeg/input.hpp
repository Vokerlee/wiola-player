/**
 * @file
 * @brief Private FFmpeg input ownership over an independent ByteStream.
 * @author Roman Glaz
 * @copyright © 2026, <vokerlee@gmail.com>
 */

#pragma once

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

class FfmpegInput final {
public:
    [[nodiscard]] static media::Result<std::unique_ptr<FfmpegInput>>
    open(std::unique_ptr<media::ByteStream> stream);

    ~FfmpegInput();

    FfmpegInput(const FfmpegInput&) = delete;
    FfmpegInput& operator=(const FfmpegInput&) = delete;
    FfmpegInput(FfmpegInput&&) = delete;
    FfmpegInput& operator=(FfmpegInput&&) = delete;

    [[nodiscard]] AVFormatContext* format() const noexcept { return format_; }

private:
    explicit FfmpegInput(std::unique_ptr<media::ByteStream> stream) noexcept;

    [[nodiscard]] media::Result<void> initialize();
    [[nodiscard]] media::Result<void> measure_size();

    static int read(void* opaque, unsigned char* output, int size) noexcept;
    static std::int64_t seek(void* opaque, std::int64_t offset, int origin) noexcept;
    static int reject_secondary_open(AVFormatContext*, AVIOContext**, const char*, int,
        AVDictionary**) noexcept;

    int read(unsigned char* output, int size) noexcept;
    std::int64_t seek(std::int64_t offset, int origin) noexcept;

    std::unique_ptr<media::ByteStream> stream_;
    AVFormatContext* format_{nullptr};
    void* io_{nullptr};
    std::uint64_t size_{0};
    std::optional<media::Error> io_error_;
};

} // namespace wiola::codec::ffmpeg
