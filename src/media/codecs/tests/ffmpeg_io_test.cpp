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

#include <media/codecs/ffmpeg/input.hpp>

#include <fixtures/temporary_directory.hpp>
#include <media/local/file_stream.hpp>

#include <gtest/gtest.h>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/mem.h>
}

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <stdexcept>
#include <stop_token>
#include <vector>

namespace {

class FaultyStream final : public wiola::media::ByteStream {
public:
    explicit FaultyStream(std::vector<std::byte> bytes)
        : bytes_{std::move(bytes)}
    {
    }

private:
    wiola::media::Result<std::size_t> read_impl(std::span<std::byte> output,
        const wiola::media::OperationContext&) override
    {
        if (throw_reads_)
            throw std::runtime_error{"injected read exception"};

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

    wiola::media::Result<void> seek_impl(std::int64_t offset, wiola::media::SeekOrigin origin,
        const wiola::media::OperationContext&) override
    {
        ++seek_calls_;

        if (throw_seeks_)
            throw std::runtime_error{"injected seek exception"};

        if (fail_seeks_ || seek_calls_ == fail_seek_at_)
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

    wiola::media::Result<std::uint64_t> tell_impl(const wiola::media::OperationContext&) override
    {
        ++tell_calls_;

        if (tell_calls_ == fail_tell_at_)
            return wiola::media::unexpected{
                wiola::media::Error{.category = wiola::media::ErrorCategory::seek_failed,
                                    .detail = "injected tell"}
            };

        if (tell_calls_ == oversized_tell_at_)
            return static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) + 1;

        return cursor_;
    }

public:
    void fail_reads() noexcept { fail_reads_ = true; }

    void throw_reads() noexcept { throw_reads_ = true; }

    void fail_seeks() noexcept { fail_seeks_ = true; }

    void fail_seek(std::size_t call) noexcept { fail_seek_at_ = call; }

    void throw_seeks() noexcept { throw_seeks_ = true; }

    void fail_tell(std::size_t call) noexcept { fail_tell_at_ = call; }

    void oversized() noexcept { oversized_tell_at_ = 2; }

    void oversized_next_tell() noexcept { oversized_tell_at_ = tell_calls_ + 1; }

    std::uint64_t bytes_read() const noexcept { return bytes_read_; }

private:
    std::vector<std::byte> bytes_;
    std::size_t cursor_{0};
    std::uint64_t bytes_read_{0};
    std::size_t seek_calls_{0};
    std::size_t fail_seek_at_{0};
    std::size_t tell_calls_{0};
    std::size_t fail_tell_at_{0};
    std::size_t oversized_tell_at_{0};
    bool fail_reads_{false};
    bool throw_reads_{false};
    bool fail_seeks_{false};
    bool throw_seeks_{false};
};

class AllocationLimit final {
public:
    explicit AllocationLimit(std::size_t bytes) { av_max_alloc(bytes); }

    ~AllocationLimit() { av_max_alloc(std::numeric_limits<std::size_t>::max()); }
};

std::vector<std::byte> fixture_bytes(const char* name = "tone.wav")
{
    const std::filesystem::path path{std::filesystem::path{WIOLA_TEST_DATA_DIR} / name};
    std::ifstream input{path, std::ios::binary};
    const std::vector<char> chars{std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{}};
    std::vector<std::byte> bytes(chars.size());
    std::memcpy(bytes.data(), chars.data(), chars.size());
    return bytes;
}

TEST(FfmpegInput, OpensAUnicodeLocalPathThroughCustomIo)
{
    wiola::testing::TemporaryDirectory directory;
    const auto path{directory.path() / std::filesystem::path{u8"музыка_файл.wav"}};
    std::filesystem::copy_file(std::filesystem::path{WIOLA_TEST_DATA_DIR} / "tone.wav", path,
        std::filesystem::copy_options::overwrite_existing);
    auto stream{wiola::media::LocalFileStream::open(path)};
    ASSERT_TRUE(stream);

    auto input{wiola::codec::ffmpeg::FfmpegInput::open(std::move(*stream))};

    ASSERT_TRUE(input);
    EXPECT_GT((*input)->format()->nb_streams, 0U);
    EXPECT_EQ((*input)->format()->pb->seekable != 0, true);
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

TEST(FfmpegInput, PropagatesFailureToRestoreTheInitialCursor)
{
    auto stream{std::make_unique<FaultyStream>(fixture_bytes())};
    stream->fail_seek(2);

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
    EXPECT_EQ(facts->output.length_accuracy, wiola::media::LengthAccuracy::exact);
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

TEST(FfmpegInput, RejectsAMissingByteStream)
{
    auto input{wiola::codec::ffmpeg::FfmpegInput::open(nullptr)};

    ASSERT_FALSE(input);
    EXPECT_EQ(input.error().category, wiola::media::ErrorCategory::unreadable);
}

TEST(FfmpegInput, ReportsFormatContextAllocationFailure)
{
    AllocationLimit limited{1};

    auto input{
        wiola::codec::ffmpeg::FfmpegInput::open(std::make_unique<FaultyStream>(fixture_bytes()))};

    ASSERT_FALSE(input);
    EXPECT_EQ(input.error().category, wiola::media::ErrorCategory::unreadable);
    EXPECT_EQ(input.error().detail, "FFmpeg input allocation failed");
}

TEST(FfmpegInput, ReportsCustomIoAllocationFailure)
{
    AllocationLimit limited{16 * 1024 - 1};

    auto input{
        wiola::codec::ffmpeg::FfmpegInput::open(std::make_unique<FaultyStream>(fixture_bytes()))};

    ASSERT_FALSE(input);
    EXPECT_EQ(input.error().category, wiola::media::ErrorCategory::unreadable);
    EXPECT_EQ(input.error().detail, "FFmpeg I/O allocation failed");
}

TEST(FfmpegInput, PropagatesInitialAndMeasuredTellFailures)
{
    for (const std::size_t call : {std::size_t{1}, std::size_t{2}}) {
        auto stream{std::make_unique<FaultyStream>(fixture_bytes())};
        stream->fail_tell(call);

        auto input{wiola::codec::ffmpeg::FfmpegInput::open(std::move(stream))};

        ASSERT_FALSE(input) << call;
        EXPECT_EQ(input.error().category, wiola::media::ErrorCategory::seek_failed) << call;
    }
}

TEST(FfmpegInput, RejectsInvalidCallbackArguments)
{
    auto input{
        wiola::codec::ffmpeg::FfmpegInput::open(std::make_unique<FaultyStream>(fixture_bytes()))};
    ASSERT_TRUE(input);
    auto* io{(*input)->format()->pb};
    std::uint8_t byte{0};

    const int null_output{io->read_packet(io->opaque, nullptr, 0)};
    const int negative_size{io->read_packet(io->opaque, &byte, -1)};
    const auto unknown_origin{io->seek(io->opaque, 0, 42)};

    EXPECT_EQ(null_output, AVERROR(EINVAL));
    EXPECT_EQ(negative_size, AVERROR(EINVAL));
    EXPECT_EQ(unknown_origin, AVERROR(EINVAL));
}

TEST(FfmpegInput, TurnsReadExceptionsIntoStableMediaErrors)
{
    auto stream{std::make_unique<FaultyStream>(fixture_bytes())};
    auto* observer{stream.get()};
    auto input{wiola::codec::ffmpeg::FfmpegInput::open(std::move(stream))};
    ASSERT_TRUE(input);
    observer->throw_reads();
    auto* io{(*input)->format()->pb};
    std::uint8_t byte{0};

    const int result{io->read_packet(io->opaque, &byte, 1)};

    EXPECT_EQ(result, AVERROR(EIO));
    ASSERT_TRUE((*input)->io_error());
    EXPECT_EQ((*input)->io_error()->category, wiola::media::ErrorCategory::read_failed);
}

TEST(FfmpegInput, PreservesSeekFailuresFromTheByteStream)
{
    auto stream{std::make_unique<FaultyStream>(fixture_bytes())};
    auto* observer{stream.get()};
    auto input{wiola::codec::ffmpeg::FfmpegInput::open(std::move(stream))};
    ASSERT_TRUE(input);
    observer->fail_seeks();
    auto* io{(*input)->format()->pb};

    const auto result{io->seek(io->opaque, 0, SEEK_SET)};

    EXPECT_EQ(result, AVERROR(EIO));
    ASSERT_TRUE((*input)->io_error());
    EXPECT_EQ((*input)->io_error()->category, wiola::media::ErrorCategory::seek_failed);
}

TEST(FfmpegInput, PreservesTellFailuresAfterASuccessfulSeek)
{
    auto stream{std::make_unique<FaultyStream>(fixture_bytes())};
    auto* observer{stream.get()};
    auto input{wiola::codec::ffmpeg::FfmpegInput::open(std::move(stream))};
    ASSERT_TRUE(input);
    observer->fail_tell(3);
    auto* io{(*input)->format()->pb};

    const auto result{io->seek(io->opaque, 0, SEEK_SET)};

    EXPECT_EQ(result, AVERROR(EIO));
    ASSERT_TRUE((*input)->io_error());
    EXPECT_EQ((*input)->io_error()->category, wiola::media::ErrorCategory::seek_failed);
}

TEST(FfmpegInput, RejectsCallbackPositionsOutsideFfmpegOffsets)
{
    auto stream{std::make_unique<FaultyStream>(fixture_bytes())};
    auto* observer{stream.get()};
    auto input{wiola::codec::ffmpeg::FfmpegInput::open(std::move(stream))};
    ASSERT_TRUE(input);
    observer->oversized_next_tell();
    auto* io{(*input)->format()->pb};

    const auto result{io->seek(io->opaque, 0, SEEK_SET)};

    EXPECT_EQ(result, AVERROR(EOVERFLOW));
}

TEST(FfmpegInput, TurnsSeekExceptionsIntoStableMediaErrors)
{
    auto stream{std::make_unique<FaultyStream>(fixture_bytes())};
    auto* observer{stream.get()};
    auto input{wiola::codec::ffmpeg::FfmpegInput::open(std::move(stream))};
    ASSERT_TRUE(input);
    observer->throw_seeks();
    auto* io{(*input)->format()->pb};

    const auto result{io->seek(io->opaque, 0, SEEK_SET)};

    EXPECT_EQ(result, AVERROR(EIO));
    ASSERT_TRUE((*input)->io_error());
    EXPECT_EQ((*input)->io_error()->category, wiola::media::ErrorCategory::seek_failed);
}

TEST(FfmpegInput, InspectionPreservesAnInjectedIoFailure)
{
    auto stream{std::make_unique<FaultyStream>(fixture_bytes())};
    auto* observer{stream.get()};

    auto input{wiola::codec::ffmpeg::FfmpegInput::open(std::move(stream))};
    ASSERT_TRUE(input);

    observer->fail_reads();
    auto* io{(*input)->format()->pb};
    std::uint8_t byte{0};
    ASSERT_EQ(io->read_packet(io->opaque, &byte, 1), AVERROR(EIO));

    auto facts{
        (*input)->inspect([](AVFormatContext*, AVDictionary**) { return AVERROR_INVALIDDATA; })};

    ASSERT_FALSE(facts);
    EXPECT_EQ(facts.error().category, wiola::media::ErrorCategory::read_failed);
}

TEST(FfmpegInput, InspectionReportsDamageWhenFfmpegFailsWithoutAnIoError)
{
    auto input{
        wiola::codec::ffmpeg::FfmpegInput::open(std::make_unique<FaultyStream>(fixture_bytes()))};
    ASSERT_TRUE(input);

    auto facts{
        (*input)->inspect([](AVFormatContext*, AVDictionary**) { return AVERROR_INVALIDDATA; })};

    ASSERT_FALSE(facts);
    EXPECT_EQ(facts.error().category, wiola::media::ErrorCategory::damaged);
}

TEST(FfmpegInput, RejectsAnAlreadyCancelledOpen)
{
    std::stop_source stop;
    stop.request_stop();
    const wiola::media::OperationContext context{.stop = stop.get_token(), .deadline = {}};

    auto input{
        wiola::codec::ffmpeg::FfmpegInput::open(std::make_unique<FaultyStream>(fixture_bytes()),
            context)};

    ASSERT_FALSE(input);
    EXPECT_EQ(input.error().category, wiola::media::ErrorCategory::cancelled);
}

std::stop_source inspection_stop;

TEST(FfmpegInput, HonorsTheFfmpegInterruptCallbackDuringInspection)
{
    inspection_stop = std::stop_source{};
    const wiola::media::OperationContext context{.stop = inspection_stop.get_token(),
        .deadline = {}};
    auto input{
        wiola::codec::ffmpeg::FfmpegInput::open(std::make_unique<FaultyStream>(fixture_bytes()),
            context)};
    ASSERT_TRUE(input);

    auto facts{(*input)->inspect([](AVFormatContext* format, AVDictionary**) {
        inspection_stop.request_stop();
        const auto interrupt{format->interrupt_callback};
        return interrupt.callback(interrupt.opaque) ? AVERROR_EXIT : 0;
    })};

    ASSERT_FALSE(facts);
    EXPECT_EQ(facts.error().category, wiola::media::ErrorCategory::cancelled);
}

} // namespace
