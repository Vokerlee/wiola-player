/**
 * @file
 * @brief Machine-readable FFmpeg qualification and fixture contract tests.
 * @author Roman Glaz
 * @copyright © 2026, <vokerlee@gmail.com>
 */

#include <codec/backends/ffmpeg/qualification.hpp>
#include <codec/decode/registry.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <string_view>

namespace {

struct Fixture {
    std::string_view format;
    std::string_view codec;
    std::string_view profile;
    std::string_view name;
    std::size_t frames;
    std::size_t channels;
};

constexpr std::array fixtures{
    Fixture{"Ogg Vorbis/Opus",           "vorbis",    "",   "tone-vorbis.ogg",  10897, 2},
    Fixture{"Ogg Vorbis/Opus",           "opus",      "",   "tone.opus",        12000, 2},
    Fixture{"MP4/M4A AAC-LC/ALAC",       "aac",       "LC", "multi-audio.m4a",  11025, 2},
    Fixture{"MP4/M4A AAC-LC/ALAC",       "alac",      "",   "tone-alac.mp4",    11025, 2},
    Fixture{"ADTS AAC-LC",               "aac",       "LC", "tone-lc.aac",      12288, 2},
    Fixture{"AIFF PCM",                  "pcm_s16be", "",   "tone-s16be.aiff",  11025, 2},
    Fixture{"AIFC PCM",                  "pcm_s16le", "",   "tone-s16le.aifc",  11025, 2},
    Fixture{"AU PCM",                    "pcm_s32be", "",   "tone-s32be.au",    11025, 2},
    Fixture{"CAF PCM",                   "pcm_f64le", "",   "tone-f64le.caf",   11025, 2},
    Fixture{"CAF PCM",                   "pcm_s16le", "",   "channels-5.1.caf", 11025, 6},
    Fixture{"Wave64 PCM",                "pcm_s24le", "",   "tone-s24le.w64",   11025, 2},
    Fixture{"RF64 PCM",                  "pcm_f32le", "",   "tone-f32le.rf64",  11025, 2},
    Fixture{"WavPack",                   "wavpack",   "",   "tone-lossless.wv", 11025, 2},
    Fixture{"Monkey's Audio",            "ape",       "",   "tone.ape",         11025, 2},
    Fixture{"TTA",                       "tta",       "",   "tone.tta",         11025, 2},
    Fixture{"ASF WMAv2",                 "wmav2",     "",   "tone-wmav2.wma",   10240, 2},
    Fixture{"Matroska/WebM Vorbis/Opus", "vorbis",    "",   "tone-vorbis.mka",  10897, 2},
    Fixture{"Matroska/WebM Vorbis/Opus", "opus",      "",   "tone-opus.mka",    12000, 2},
    Fixture{"WAV PCM",                   "pcm_s16le", "",   "tone.wav",         11025, 2},
    Fixture{"FLAC",                      "flac",      "",   "tone.flac",        11025, 2},
    Fixture{"MPEG audio Layer II/III",   "mp2",       "",   "tone.mp2",         11520, 2},
    Fixture{"MPEG audio Layer II/III",   "mp3",       "",   "tone.mp3",         11025, 2},
};

bool matches(const wiola::codec::ffmpeg::Qualification& qualification, const Fixture& fixture)
{
    return qualification.format == fixture.format && qualification.codec == fixture.codec &&
        qualification.profile == fixture.profile;
}

wiola::media::Result<wiola::pcm::Frames> read_all(wiola::codec::Decoder& decoder)
{
    wiola::pcm::Frames total;
    std::array<float, 126> block{};
    for (int calls{0}; calls < 10000; ++calls) {
        auto read{decoder.read(block)};
        if (!read)
            return wiola::media::unexpected{read.error()};
        total += read->frames;
        if (read->end_of_stream)
            return total;
    }
    return wiola::media::unexpected{
        wiola::media::Error{.category = wiola::media::ErrorCategory::limit_exceeded,
                            .detail = "qualification decode did not finish"}
    };
}

TEST(FfmpegQualification, EveryMatrixEntryHasOnePassingRealFixture)
{
    const wiola::codec::MediaBackendRegistry registry;
    for (const auto& qualification : wiola::codec::ffmpeg::qualifications()) {
        std::size_t matches_found{0};
        for (const auto& fixture : fixtures) {
            if (!matches(qualification, fixture))
                continue;
            ++matches_found;
            auto decoder{registry.open(std::filesystem::path{WIOLA_TEST_DATA_DIR} / fixture.name)};
            ASSERT_TRUE(decoder) << fixture.name;
            EXPECT_EQ((*decoder)->spec().num_channels, fixture.channels) << fixture.name;
            auto frames{read_all(**decoder)};
            ASSERT_TRUE(frames) << fixture.name;
            EXPECT_EQ(*frames, wiola::pcm::Frames{fixture.frames}) << fixture.name;
        }
        EXPECT_EQ(matches_found, 1U) << qualification.format << '/' << qualification.codec;
    }
    for (const auto& fixture : fixtures) {
        EXPECT_TRUE(
            std::ranges::any_of(wiola::codec::ffmpeg::qualifications(),
                [&](const auto& qualification) { return matches(qualification, fixture); })
        ) << fixture.name;
    }
}

TEST(FfmpegQualification, MatrixAndBackendDescriptorsCoverEachOtherExactly)
{
    const wiola::codec::MediaBackendRegistry registry;
    const auto backend{std::ranges::find_if(registry.backends(),
        [](const auto& candidate) { return candidate.name == "FFmpeg"; })};
    ASSERT_NE(backend, registry.backends().end());
    for (const auto& format : backend->formats) {
        EXPECT_TRUE(
            std::ranges::any_of(wiola::codec::ffmpeg::qualifications(),
                [&](const auto& qualification) { return qualification.format == format.name; })
        ) << format.name;
    }
    for (const auto& qualification : wiola::codec::ffmpeg::qualifications()) {
        EXPECT_TRUE(std::ranges::any_of(backend->formats, [&](const auto& format) {
            return qualification.format == format.name;
        })) << qualification.format;
    }
}

TEST(FfmpegQualification, RejectsUnqualifiedCodecsAndAacProfiles)
{
    using wiola::codec::ffmpeg::is_qualified;
    EXPECT_TRUE(is_qualified("aac", "aac", "LC"));
    EXPECT_FALSE(is_qualified("aac", "aac", "HE-AAC"));
    EXPECT_FALSE(is_qualified("ogg", "speex", ""));
    EXPECT_FALSE(is_qualified("mpc8", "mpc8", ""));
}

} // namespace
