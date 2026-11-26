/**
 * @file
 * @brief Real decoder/metadata provider integration with the playback Session.
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

#include <media/codecs/ffmpeg/composition.hpp>

#include <fakes/output.hpp>
#include <fixtures/wav.hpp>
#include <wiola/audio/stage.hpp>
#include <wiola/media/local/resource.hpp>
#include <wiola/playback/session.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <memory>
#include <thread>
#include <vector>

namespace {

class PassThrough final : public wiola::audio::Stage {
public:
    std::unique_ptr<Preparation> prepare(wiola::pcm::StreamSpec) noexcept override { return {}; }

    void process(std::span<float>) noexcept override { }
};

class CodecSession : public ::testing::Test {
protected:
    std::vector<wiola::pcm::StreamSpec> outputs;
    PassThrough processing;
    wiola::engine::Session session{processing,
        [this](wiola::audio::Source& source) {
            outputs.push_back(source.spec());
            return std::make_unique<wiola::testing::FakeOutput>();
        },
        wiola::codec::ffmpeg::make_provider()};

    template<class Predicate>
    bool pump_until(Predicate settled)
    {
        const auto deadline{std::chrono::steady_clock::now() + std::chrono::seconds{5}};
        do {
            session.catch_up();
            if (settled())
                return true;
            std::this_thread::yield();
        } while (std::chrono::steady_clock::now() < deadline);
        return settled();
    }

    bool open(const std::filesystem::path& path)
    {
        session.open({wiola::media::local_resource(path)});
        return pump_until([this] {
            return !session.reading() &&
                session.open_result() != wiola::engine::OpenResult::loading;
        });
    }

    static std::vector<std::filesystem::path> metadata_tracks()
    {
        const std::filesystem::path root{WIOLA_TEST_DATA_DIR};
        return {root / "tone.flac", root / "tone.wav"};
    }

    bool all_metadata_ready(std::size_t count)
    {
        const auto queued{session.playlist().tracks()};
        return queued.size() == count && std::ranges::all_of(queued, [](const auto& entry) {
            return entry.metadata().duration && *entry.metadata().duration > wiola::units::Time{} &&
                !entry.metadata().title.empty();
        });
    }
};

TEST_F(CodecSession, DecoderAndMetadataReachTheSessionThroughTheProviderPort)
{
    const auto path{wiola::testing::write_wav("wiola_codec_session.wav")};
    ASSERT_TRUE(open(path));
    ASSERT_EQ(session.open_result(), wiola::engine::OpenResult::opened);
    EXPECT_TRUE(session.loaded());
    EXPECT_EQ(session.track(), wiola::media::local_resource(path));
    ASSERT_TRUE(session.total_time());
    EXPECT_NEAR(session.total_time()->seconds(), wiola::testing::source_seconds, 0.01);
    ASSERT_TRUE(pump_until([this] { return all_metadata_ready(1); }));
    EXPECT_EQ(session.playlist().current()->metadata().title, path.stem().string());
}

TEST_F(CodecSession, RealStreamShapeControlsOutputReuse)
{
    const auto first{wiola::testing::write_wav("wiola_codec_session_first.wav", 44100)};
    const auto changed{wiola::testing::write_wav("wiola_codec_session_changed.wav", 48000)};
    ASSERT_TRUE(open(first));
    ASSERT_EQ(session.open_result(), wiola::engine::OpenResult::opened);
    ASSERT_TRUE(open(first));
    ASSERT_EQ(session.open_result(), wiola::engine::OpenResult::opened);
    ASSERT_EQ(outputs.size(), 1U);
    EXPECT_EQ(outputs[0].sample_rate, wiola::units::Frequency{44100.0});
    ASSERT_TRUE(open(changed));
    ASSERT_EQ(session.open_result(), wiola::engine::OpenResult::opened);
    ASSERT_EQ(outputs.size(), 2U);
    EXPECT_EQ(outputs[1].sample_rate, wiola::units::Frequency{48000.0});
}

TEST_F(CodecSession, OpenedQueueReceivesMetadataForEveryFormat)
{
    const auto tracks{metadata_tracks()};
    session.open(wiola::media::local_resources(tracks));
    ASSERT_TRUE(pump_until([&] { return all_metadata_ready(tracks.size()); }));
    EXPECT_EQ(session.playlist().tracks().front().resource(),
        wiola::media::local_resource(tracks.front()));
    EXPECT_EQ(session.playlist().tracks().back().resource(),
        wiola::media::local_resource(tracks.back()));
}

TEST_F(CodecSession, AppendedQueueReceivesMetadataForEveryFormat)
{
    const auto tracks{metadata_tracks()};
    session.add(wiola::media::local_resources(tracks));
    ASSERT_TRUE(pump_until([&] { return all_metadata_ready(tracks.size()); }));
    EXPECT_EQ(session.playlist().tracks().front().resource(),
        wiola::media::local_resource(tracks.front()));
    EXPECT_EQ(session.playlist().tracks().back().resource(),
        wiola::media::local_resource(tracks.back()));
}

TEST_F(CodecSession, UnreadableResourcePreservesTheInstalledRealDecoder)
{
    const auto path{wiola::testing::write_wav("wiola_codec_session_kept.wav")};
    ASSERT_TRUE(open(path));
    ASSERT_EQ(session.open_result(), wiola::engine::OpenResult::opened);
    ASSERT_TRUE(session.play());
    ASSERT_TRUE(pump_until([this] { return session.playing(); }));
    const auto missing{
        path.parent_path() / (path.stem().string() + "-missing" + path.extension().string())};
    ASSERT_TRUE(open(missing));
    EXPECT_EQ(session.open_result(), wiola::engine::OpenResult::unreadable);
    EXPECT_TRUE(session.loaded());
    EXPECT_TRUE(session.playing());
    EXPECT_EQ(session.track(), wiola::media::local_resource(path));
    EXPECT_EQ(outputs.size(), 1U);
}

} // namespace
