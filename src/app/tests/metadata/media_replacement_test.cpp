/**
 * @file
 * @brief Validated atomic media replacement regression tests.
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

#include <app/metadata/media_replacement.hpp>
#include <media/codecs/ffmpeg/metadata.hpp>
#include <media/codecs/ffmpeg/metadata_writer.hpp>
#include <wiola/media/audio_decoder.hpp>
#include <wiola/media/codecs/ffmpeg/composition.hpp>
#include <wiola/media/local/fingerprint.hpp>
#include <wiola/media/local/resource.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

namespace {

std::filesystem::path temporary(const char* name)
{
    const auto path = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove(path);

    return path;
}

struct Cleanup {
    std::vector<std::filesystem::path> paths;

    ~Cleanup()
    {
        for (const auto& path : paths)
            std::filesystem::remove(path);
    }
};

std::vector<char> bytes(const std::filesystem::path& path)
{
    std::ifstream input{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

wiola::media::MetadataWriteRequest title_request()
{
    wiola::media::MetadataWriteRequest request;
    request.overrides.title = "Replaced title";

    return request;
}

TEST(MediaReplacement, ValidatesAndCommitsWhileRetainingTheOriginal)
{
    const auto source = temporary("wiola-replacement.mp3");
    const auto output = temporary("wiola-replacement-output.mp3");
    auto backup = source;
    backup += ".wiola-backup";

    Cleanup cleanup{
        {source, output, backup}
    };

    std::filesystem::copy_file(std::filesystem::path{WIOLA_TEST_DATA_DIR} / "tone.mp3", source);

    const auto original = bytes(source);
    const auto expected = wiola::media::fingerprint_media(source);

    ASSERT_TRUE(expected);

    ASSERT_EQ(wiola::codec::ffmpeg::remux_metadata(source, output, title_request()).state,
        wiola::media::MetadataWriteState::written);

    auto provider{wiola::codec::ffmpeg::make_provider()};
    auto replacement = wiola::media::native_file_replacement(wiola::media::fingerprint_media);
    const auto outcome =
        wiola::app::commit_media_replacement(source, output, *provider, *replacement, *expected);

    ASSERT_EQ(outcome.state, wiola::media::MetadataWriteState::written) << outcome.detail;
    EXPECT_TRUE(outcome.source_modified);
    EXPECT_FALSE(std::filesystem::exists(output));
    EXPECT_EQ(bytes(backup), original);

    const auto metadata = wiola::codec::ffmpeg::read_metadata(source);

    ASSERT_TRUE(metadata);
    EXPECT_EQ(metadata->track.title, "Replaced title");
}

TEST(MediaReplacement, RejectsInvalidOutputWithoutChangingTheSource)
{
    const auto source = temporary("wiola-invalid-replacement.mp3");
    const auto output = temporary("wiola-invalid-replacement-output.mp3");
    auto backup = source;
    backup += ".wiola-backup";

    Cleanup cleanup{
        {source, output, backup}
    };

    std::filesystem::copy_file(std::filesystem::path{WIOLA_TEST_DATA_DIR} / "tone.mp3", source);

    const auto original = bytes(source);
    {
        std::ofstream invalid{output, std::ios::binary};
        invalid << "invalid media";
    }

    auto provider{wiola::codec::ffmpeg::make_provider()};
    auto replacement = wiola::media::native_file_replacement(wiola::media::fingerprint_media);
    const auto outcome =
        wiola::app::commit_media_replacement(source, output, *provider, *replacement);

    EXPECT_EQ(outcome.state, wiola::media::MetadataWriteState::failed);
    EXPECT_EQ(outcome.error, wiola::media::MetadataWriteError::invalid_output);
    EXPECT_FALSE(outcome.source_modified);
    EXPECT_EQ(bytes(source), original);
    EXPECT_FALSE(std::filesystem::exists(output));
    EXPECT_FALSE(std::filesystem::exists(backup));
}

TEST(MediaReplacement, RejectsAnExternalEditBeforeCommit)
{
    const auto source = temporary("wiola-conflicted-replacement.mp3");
    const auto output = temporary("wiola-conflicted-replacement-output.mp3");
    auto backup = source;
    backup += ".wiola-backup";

    Cleanup cleanup{
        {source, output, backup}
    };

    std::filesystem::copy_file(std::filesystem::path{WIOLA_TEST_DATA_DIR} / "tone.mp3", source);

    const auto expected = wiola::media::fingerprint_media(source);

    ASSERT_TRUE(expected);
    ASSERT_EQ(wiola::codec::ffmpeg::remux_metadata(source, output, title_request()).state,
        wiola::media::MetadataWriteState::written);

    {
        std::ofstream changed{source, std::ios::app};
        changed << "external edit";
    }

    const auto changed = bytes(source);

    auto provider{wiola::codec::ffmpeg::make_provider()};
    auto replacement = wiola::media::native_file_replacement(wiola::media::fingerprint_media);
    const auto outcome =
        wiola::app::commit_media_replacement(source, output, *provider, *replacement, *expected);

    EXPECT_EQ(outcome.state, wiola::media::MetadataWriteState::failed);
    EXPECT_EQ(outcome.error, wiola::media::MetadataWriteError::conflicted);
    EXPECT_FALSE(outcome.source_modified);
    EXPECT_EQ(bytes(source), changed);
    EXPECT_FALSE(std::filesystem::exists(backup));
}

} // namespace

TEST(MediaReplacement, LockedSourceDoesNotReachTheCommit)
{
    const auto source = temporary("wiola-locked-replacement.mp3");
    const auto output = temporary("wiola-locked-replacement-output.mp3");
    auto backup = source;
    backup += ".wiola-backup";

    Cleanup cleanup{
        {source, output, backup}
    };

    std::filesystem::copy_file(std::filesystem::path{WIOLA_TEST_DATA_DIR} / "tone.mp3", source);

    const auto original = bytes(source);

    ASSERT_EQ(wiola::codec::ffmpeg::remux_metadata(source, output, title_request()).state,
        wiola::media::MetadataWriteState::written);

    auto provider{wiola::codec::ffmpeg::make_provider()};
    auto replacement = wiola::media::native_file_replacement(wiola::media::fingerprint_media);
    wiola::app::MediaReplacementHooks hooks;
    hooks.before_commit = [] {
        return false;
    };

    const auto outcome = wiola::app::commit_media_replacement(source, output, *provider,
        *replacement, {}, {}, hooks);

    EXPECT_EQ(outcome.state, wiola::media::MetadataWriteState::failed);
    EXPECT_EQ(outcome.error, wiola::media::MetadataWriteError::io);
    EXPECT_FALSE(outcome.source_modified);
    EXPECT_EQ(bytes(source), original);
    EXPECT_FALSE(std::filesystem::exists(backup));
}

namespace {

class ChunkDecoder final : public wiola::media::AudioDecoder {
public:
    explicit ChunkDecoder(std::size_t chunk, bool changed)
        : AudioDecoder{wiola::pcm::StreamSpec{}, wiola::pcm::Frames{3000}}
        , chunk_{chunk}
        , changed_{changed}
    {
    }

protected:
    wiola::media::Result<wiola::media::DecodeOutcome> decode(std::span<float> output,
        wiola::pcm::Frames requested) override
    {
        const auto count = wiola::pcm::Frames{std::min(requested.count(), chunk_)};
        const auto start = spec().samples_per(position_frames());

        for (std::size_t i = 0; i < spec().samples_per(count); ++i)
            output[i] = static_cast<float>((start + i) % 17) / 17.0F;

        if (changed_ && start == 0)
            output[0] = 100.0F;

        return wiola::media::DecodeOutcome{.frames = count};
    }

    wiola::media::Result<wiola::media::SeekOutcome> seek_frame(wiola::pcm::Frames frame) override
    {
        return wiola::media::SeekOutcome{frame, frame, true};
    }

private:
    std::size_t chunk_;
    bool changed_;
};

class ChunkAsset final : public wiola::media::MediaAsset {
public:
    ChunkAsset(std::size_t chunk, bool changed)
        : chunk_{chunk}
        , changed_{changed}
    {
    }

    wiola::media::Result<wiola::media::MediaInfo>
    inspect(const wiola::media::OperationContext&) override
    {
        return wiola::media::MediaInfo{};
    }

    wiola::media::Result<wiola::media::Metadata>
    read_metadata(const wiola::media::OperationContext&) override
    {
        return wiola::media::Metadata{};
    }

    wiola::media::Result<std::unique_ptr<wiola::media::AudioDecoder>>
    create_decoder(const wiola::media::OperationContext&) override
    {
        return std::unique_ptr<wiola::media::AudioDecoder>{
            std::make_unique<ChunkDecoder>(chunk_, changed_)};
    }

private:
    std::size_t chunk_;
    bool changed_;
};

class ChunkProvider final : public wiola::media::MediaProvider {
public:
    std::filesystem::path source;
    bool changed{};

    wiola::media::Result<std::unique_ptr<wiola::media::MediaAsset>> open(
        const wiola::media::ResourceReference& resource, const wiola::media::OperationContext&
    ) override
    {
        const auto path{wiola::media::local_path(resource).value()};
        return std::unique_ptr<wiola::media::MediaAsset>{
            std::make_unique<ChunkAsset>(path == source ? 137 : 701, path != source && changed)};
    }

    std::vector<std::string> playback_extensions() const override { return {}; }
};

TEST(MediaReplacement, ComparesOrderedSamplesAcrossDifferentDecoderBlockBoundaries)
{
    const auto source = temporary("wiola-variable-source.bin"),
               output = temporary("wiola-variable-output.bin");
    Cleanup cleanup{
        {source, output}
    };

    std::ofstream{source} << "source";
    std::ofstream{output} << "output";

    ChunkProvider provider;
    provider.source = source;
    EXPECT_TRUE(wiola::app::prepare_media_replacement(source, output, provider).prepared);

    provider.changed = true;

    const auto invalid = wiola::app::prepare_media_replacement(source, output, provider);

    EXPECT_FALSE(invalid.prepared);
    EXPECT_EQ(invalid.failure.error, wiola::media::MetadataWriteError::invalid_output);
}

TEST(MediaReplacement, RechecksCapturedSourceImmediatelyBeforePreparedCommit)
{
    const auto source = temporary("wiola-prepared-source.bin"),
               output = temporary("wiola-prepared-output.bin");
    Cleanup cleanup{
        {source, output}
    };

    std::ofstream{source} << "source";
    std::ofstream{output} << "output";

    ChunkProvider provider;
    provider.source = source;

    const auto validation = wiola::app::prepare_media_replacement(source, output, provider);

    ASSERT_TRUE(validation.prepared);

    std::ofstream{source, std::ios::app} << "external metadata";

    const auto external = bytes(source);
    auto replacement = wiola::media::native_file_replacement(wiola::media::fingerprint_media);
    const auto result =
        wiola::app::commit_prepared_media_replacement(*validation.prepared, *replacement);

    EXPECT_EQ(result.error, wiola::media::MetadataWriteError::conflicted);
    EXPECT_EQ(bytes(source), external);
}

class RefusingReplacement final : public wiola::media::FileReplacement {
public:
    wiola::media::FileReplacementResult replace(wiola::media::FileReplacementRequest request,
        const std::function<bool()>& guard) override
    {
        captured = std::move(request);
        EXPECT_TRUE(guard());

        return {.error = wiola::media::FileReplacementError::io, .detail = "driver refused"};
    }

    wiola::media::FileReplacementResult recover(const std::filesystem::path&) override
    {
        ADD_FAILURE() << "replacement policy must not choose a recovery mechanism";
        return {};
    }

    std::optional<wiola::media::FileReplacementRequest> captured;
};

TEST(MediaReplacement, SuppliesValidatedOwnedInputsToTheSelectedDriverWithoutNativeFallback)
{
    const auto source = temporary("wiola-driver-source.bin");
    const auto output = temporary("wiola-driver-output.bin");
    Cleanup cleanup{
        {source, output}
    };

    std::ofstream{source} << "source";
    std::ofstream{output} << "output";

    ChunkProvider provider;
    provider.source = source;

    const auto validation = wiola::app::prepare_media_replacement(source, output, provider);

    ASSERT_TRUE(validation.prepared);

    const auto original = bytes(source);
    RefusingReplacement driver;

    const auto outcome = wiola::app::commit_prepared_media_replacement(*validation.prepared, driver,
        {}, {.before_commit = {}, .backup_suffix = "operation"});

    ASSERT_TRUE(driver.captured);
    EXPECT_EQ(driver.captured->source, source);
    EXPECT_EQ(driver.captured->output, output);
    EXPECT_EQ(driver.captured->backup, source.string() + ".wiola-backup.operation");
    EXPECT_EQ(driver.captured->original_digest, validation.prepared->source_fingerprint);
    EXPECT_EQ(driver.captured->output_digest, validation.prepared->output_fingerprint);
    EXPECT_EQ(outcome.error, wiola::media::MetadataWriteError::io);
    EXPECT_FALSE(outcome.source_modified);
    EXPECT_EQ(outcome.detail, "driver refused");
    EXPECT_EQ(bytes(source), original);
    EXPECT_TRUE(std::filesystem::exists(output));
}

} // namespace
