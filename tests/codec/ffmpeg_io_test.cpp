/**
 * @file
 * @brief Custom FFmpeg ByteStream I/O ownership and failure tests.
 * @author Roman Glaz
 * @copyright © 2026, <vokerlee@gmail.com>
 */

#include <codec/backends/ffmpeg/input.hpp>

#include <media/local_file_stream.hpp>

#include <gtest/gtest.h>

extern "C" {
#include <libavformat/avformat.h>
}

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <vector>

namespace {

class FaultyStream final : public wiola::media::ByteStream {
public:
    explicit FaultyStream(std::vector<std::byte> bytes)
        : bytes_{std::move(bytes)}
    {
    }

    wiola::media::Result<std::size_t> read(std::span<std::byte> output) override
    {
        if (fail_reads_)
            return wiola::media::unexpected{
                wiola::media::Error{.category = wiola::media::ErrorCategory::read_failed,
                                    .detail = "injected read"}
            };
        const std::size_t count{std::min(output.size(), bytes_.size() - cursor_)};
        std::copy_n(bytes_.begin() + static_cast<std::ptrdiff_t>(cursor_), count, output.begin());
        cursor_ += count;
        return count;
    }

    wiola::media::Result<void> seek(std::int64_t offset, wiola::media::SeekOrigin origin) override
    {
        if (fail_seeks_)
            return wiola::media::unexpected{
                wiola::media::Error{.category = wiola::media::ErrorCategory::seek_failed,
                                    .detail = "injected seek"}
            };
        const std::int64_t base{origin == wiola::media::SeekOrigin::start ? 0
                : origin == wiola::media::SeekOrigin::current
                ? static_cast<std::int64_t>(cursor_)
                : static_cast<std::int64_t>(bytes_.size())};
        const std::int64_t target{base + offset};
        if (target < 0)
            return wiola::media::unexpected{
                wiola::media::Error{.category = wiola::media::ErrorCategory::seek_failed,
                                    .detail = "negative cursor"}
            };
        cursor_ = static_cast<std::size_t>(target);
        return {};
    }

    wiola::media::Result<std::uint64_t> tell() override
    {
        if (oversized_)
            return static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) + 1;
        return cursor_;
    }

    void fail_reads() noexcept { fail_reads_ = true; }

    void fail_seeks() noexcept { fail_seeks_ = true; }

    void oversized() noexcept { oversized_ = true; }

private:
    std::vector<std::byte> bytes_;
    std::size_t cursor_{0};
    bool fail_reads_{false};
    bool fail_seeks_{false};
    bool oversized_{false};
};

std::vector<std::byte> fixture_bytes()
{
    const std::filesystem::path path{std::filesystem::path{WIOLA_TEST_DATA_DIR} / "tone.wav"};
    std::ifstream input{path, std::ios::binary};
    const std::vector<char> chars{std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{}};
    std::vector<std::byte> bytes(chars.size());
    std::memcpy(bytes.data(), chars.data(), chars.size());
    return bytes;
}

TEST(FfmpegInput, OpensAUnicodeLocalPathThroughCustomIo)
{
    const std::filesystem::path path{
        std::filesystem::temp_directory_path() / "wiola_музыка_файл.wav"};
    std::filesystem::copy_file(std::filesystem::path{WIOLA_TEST_DATA_DIR} / "tone.wav", path,
        std::filesystem::copy_options::overwrite_existing);
    auto stream{wiola::media::LocalFileStream::open(path)};
    ASSERT_TRUE(stream);

    auto input{wiola::codec::ffmpeg::FfmpegInput::open(std::move(*stream))};

    ASSERT_TRUE(input);
    EXPECT_GT((*input)->format()->nb_streams, 0U);
    EXPECT_EQ((*input)->format()->pb->seekable != 0, true);
    std::filesystem::remove(path);
}

TEST(FfmpegInput, RejectsEverySecondaryOpen)
{
    auto input{
        wiola::codec::ffmpeg::FfmpegInput::open(std::make_unique<FaultyStream>(fixture_bytes()))};
    ASSERT_TRUE(input);
    AVIOContext* nested{nullptr};

    const int result{(*input)->format()->io_open((*input)->format(), &nested,
        "https://example.invalid/track.wav", AVIO_FLAG_READ, nullptr)};

    EXPECT_LT(result, 0);
    EXPECT_EQ(nested, nullptr);
}

TEST(FfmpegInput, PreservesInjectedReadFailure)
{
    auto stream{std::make_unique<FaultyStream>(fixture_bytes())};
    stream->fail_reads();

    auto input{wiola::codec::ffmpeg::FfmpegInput::open(std::move(stream))};

    ASSERT_FALSE(input);
    EXPECT_EQ(input.error().category, wiola::media::ErrorCategory::read_failed);
}

TEST(FfmpegInput, PreservesInjectedSeekFailure)
{
    auto stream{std::make_unique<FaultyStream>(fixture_bytes())};
    stream->fail_seeks();

    auto input{wiola::codec::ffmpeg::FfmpegInput::open(std::move(stream))};

    ASSERT_FALSE(input);
    EXPECT_EQ(input.error().category, wiola::media::ErrorCategory::seek_failed);
}

TEST(FfmpegInput, RejectsSizesOutsideFfmpegOffsets)
{
    auto stream{std::make_unique<FaultyStream>(fixture_bytes())};
    stream->oversized();

    auto input{wiola::codec::ffmpeg::FfmpegInput::open(std::move(stream))};

    ASSERT_FALSE(input);
    EXPECT_EQ(input.error().category, wiola::media::ErrorCategory::seek_failed);
}

} // namespace
