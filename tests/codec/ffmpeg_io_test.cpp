/**
 * @file
 * @brief Custom FFmpeg ByteStream I/O ownership and failure tests.
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
        bytes_read_ += count;
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

    std::uint64_t bytes_read() const noexcept { return bytes_read_; }

private:
    std::vector<std::byte> bytes_;
    std::size_t cursor_{0};
    std::uint64_t bytes_read_{0};
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

wiola::media::Result<wiola::codec::ffmpeg::FfmpegStreamFacts> inspect_fixture(const char* name)
{
    auto stream{
        wiola::media::LocalFileStream::open(std::filesystem::path{WIOLA_TEST_DATA_DIR} / name)};
    if (!stream)
        return wiola::media::unexpected{stream.error()};
    auto input{wiola::codec::ffmpeg::FfmpegInput::open(std::move(*stream))};
    if (!input)
        return wiola::media::unexpected{input.error()};
    return (*input)->inspect();
}

TEST(FfmpegInspection, SelectsExactStereoPcmFacts)
{
    auto facts{inspect_fixture("tone.wav")};

    ASSERT_TRUE(facts);
    EXPECT_EQ(facts->output.spec.sample_rate, wiola::units::Frequency{44100.0});
    EXPECT_EQ(facts->output.spec.num_channels, 2U);
    EXPECT_EQ(facts->output.length, wiola::pcm::Frames{11025});
    EXPECT_EQ(facts->output.length_accuracy, wiola::codec::LengthAccuracy::exact);
    EXPECT_TRUE(facts->output.seekable);
}

TEST(FfmpegInspection, RejectsNoAudioAndUnsupportedAudioSeparatelyFromDamage)
{
    auto no_audio{inspect_fixture("no-audio.mp4")};
    auto unsupported{inspect_fixture("unsupported-speex.ogg")};

    ASSERT_FALSE(no_audio);
    ASSERT_FALSE(unsupported);
    EXPECT_EQ(no_audio.error().category, wiola::media::ErrorCategory::unsupported);
    EXPECT_EQ(unsupported.error().category, wiola::media::ErrorCategory::unsupported);
    EXPECT_EQ(unsupported.error().detail, "no supported audio stream or channel layout");
}

TEST(FfmpegInspection, ChoosesTheDefaultPlayableAudioStream)
{
    auto facts{inspect_fixture("multi-audio.m4a")};

    ASSERT_TRUE(facts);
    EXPECT_EQ(facts->stream_index, 0U);
    EXPECT_EQ(facts->output.spec.num_channels, 2U);
}

TEST(FfmpegInspection, NeverSelectsAttachedArtworkAsAudio)
{
    auto facts{inspect_fixture("attached-art.m4a")};

    ASSERT_TRUE(facts);
    EXPECT_EQ(facts->stream_index, 0U);
    EXPECT_EQ(facts->output.spec.num_channels, 2U);
}

TEST(FfmpegInspection, DoesNotScanLargeInputsDuringStartup)
{
    auto bytes{fixture_bytes()};
    std::size_t data_marker{0};
    for (std::size_t index{0}; index + 4 <= bytes.size(); ++index) {
        if (std::memcmp(bytes.data() + index, "data", 4) == 0) {
            data_marker = index;
            break;
        }
    }
    ASSERT_NE(data_marker, 0U);
    const std::size_t payload_offset{data_marker + 8};
    bytes.resize(1024 * 1024);
    const auto write_size{[&](std::size_t offset, std::size_t value) {
        for (std::size_t byte{0}; byte < 4; ++byte)
            bytes[offset + byte] = static_cast<std::byte>(value >> (byte * 8));
    }};
    write_size(4, bytes.size() - 8);
    write_size(data_marker + 4, bytes.size() - payload_offset);
    auto stream{std::make_unique<FaultyStream>(std::move(bytes))};
    auto* observer{stream.get()};
    auto input{wiola::codec::ffmpeg::FfmpegInput::open(std::move(stream))};
    ASSERT_TRUE(input);

    auto facts{(*input)->inspect()};

    ASSERT_TRUE(facts);
    EXPECT_LT(observer->bytes_read(), 1024U * 1024U / 2U);
}

} // namespace
