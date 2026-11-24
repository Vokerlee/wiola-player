/**
 * @file
 * @brief Standalone playback pipeline and output lifetime tests.
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

#include <engine/pipeline/output_session.hpp>
#include <engine/pipeline/playback_pipeline.hpp>
#include <fakes/decoder.hpp>
#include <fakes/output.hpp>
#include <wiola/audio/processing_chain.hpp>
#include <wiola/media/stream_spec.hpp>
#include <wiola/units.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <memory>
#include <stdexcept>

namespace {

using wiola::audio::ProcessingChain;
using wiola::engine::OutputSession;
using wiola::engine::PlaybackPipeline;
using wiola::pcm::Frames;
using wiola::pcm::StreamSpec;
using namespace wiola::units;

constexpr StreamSpec stereo{.sample_rate = 48_kHz, .num_channels = 2};
constexpr StreamSpec mono{.sample_rate = 44100_Hz, .num_channels = 1};

TEST(OutputSession, ReusesAnOutputForTheSameStreamSpec)
{
    ProcessingChain chain;
    std::size_t num_outputs{0};
    OutputSession session{chain, [&num_outputs](wiola::audio::Source&) {
                              ++num_outputs;
                              return std::make_unique<wiola::testing::FakeOutput>();
                          }};

    session.open(stereo);
    wiola::audio::Output* first{&session.output()};
    session.open(stereo);

    EXPECT_EQ(&session.output(), first);
    EXPECT_EQ(num_outputs, 1U);

    session.open(mono);

    EXPECT_EQ(num_outputs, 2U);
}

TEST(OutputSession, CleansUpAFailedReplacement)
{
    ProcessingChain chain;
    bool fail{false};
    OutputSession session{chain,
        [&fail](wiola::audio::Source&) -> std::unique_ptr<wiola::audio::Output> {
            if (fail)
                return nullptr;
            return std::make_unique<wiola::testing::FakeOutput>();
        }};
    session.open(stereo);
    fail = true;

    EXPECT_THROW(session.open(mono), std::runtime_error);
    EXPECT_FALSE(session.open_for(stereo));
    EXPECT_FALSE(session.open_for(mono));
}

TEST(PlaybackPipeline, OwnsAPlayableTrackWithoutASession)
{
    ProcessingChain chain;
    OutputSession output{chain,
        [](wiola::audio::Source&) { return std::make_unique<wiola::testing::FakeOutput>(); }};
    output.open(stereo);
    auto source{std::make_unique<wiola::testing::FakeDecoder>(Frames{64}, stereo)};
    PlaybackPipeline pipeline{std::move(source), output.output(), Frames{64}};
    output.connect(pipeline.decoded());

    ASSERT_TRUE(pipeline.player().start());
    pipeline.player().stop();
    pipeline.player().wait();

    EXPECT_TRUE(pipeline.player().finished());
}

} // namespace
