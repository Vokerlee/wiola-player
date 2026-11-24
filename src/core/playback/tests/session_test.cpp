/**
 * @file
 * @brief Unit tests for the session.
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

#include <fakes/provider.hpp>
#include <fixtures/shaping.hpp>
#include <wiola/audio/processing_chain.hpp>

#include <wiola/audio/output.hpp>

#include <wiola/audio/source.hpp>
#include <wiola/media/audio_decoder.hpp>
#include <wiola/media/stream_spec.hpp>
#include <wiola/playback/session.hpp>

#include <allocation_failure.hpp>
#include <fakes/decoder.hpp>
#include <fakes/output.hpp>
#include <wiola/playback/queue_entry.hpp>
#include <wiola/units.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>

namespace {

using namespace std::chrono_literals;
using namespace wiola::units::literals;
using wiola::engine::OpenResult;
using wiola::pcm::Frames;
using State = wiola::engine::PlaybackState;
using wiola::engine::Session;
namespace units = wiola::units;

static_assert(!noexcept(std::declval<const Session&>().snapshot()));
static_assert(!std::is_default_constructible_v<Session>);
static_assert(!std::is_constructible_v<Session, wiola::engine::OutputFactory>);

/// Loads and waits for it, the way a window that draws does: asking, then taking the answer up
/// on a later turn.
OpenResult load_and_wait(Session& session, const std::filesystem::path& path)
{
    session.open({path});

    while (session.reading())
        std::this_thread::yield();

    session.catch_up();

    return session.open_result();
}

/// Turns the session over until `settled` says it has caught up, the way a window that draws
/// does. Fails rather than hangs when it never does.
template<typename Predicate>
bool poll_until(Session& session, Predicate settled)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};

    while (std::chrono::steady_clock::now() < deadline) {
        session.catch_up();

        if (settled())
            return true;

        std::this_thread::yield();
    }

    return settled();
}

/// Supplies decoded-stream facts, not encoded files. The store outlives Session's owned workers.
struct Fixture {
private:
    mutable std::mutex media_mutex_;
    std::map<std::filesystem::path, wiola::media::DecoderStreamInfo> media_;
    std::size_t next_resource_{};

    std::optional<wiola::media::DecoderStreamInfo> lookup(const std::filesystem::path& path) const
    {
        const std::lock_guard lock{media_mutex_};
        const auto found{media_.find(path)};
        return found == media_.end() ? std::nullopt : std::optional{found->second};
    }

    static wiola::media::unexpected missing()
    {
        return wiola::media::unexpected{
            wiola::media::Error{wiola::media::ErrorCategory::unreadable,
                                "controlled resource was not supplied"}
        };
    }

public:
    // Longer than priming and the seek positions exercised by these controlled cases.
    static constexpr units::Time duration{std::chrono::milliseconds{1500}};

    Fixture()
        : session{processing.chain,
              [this](wiola::audio::Source& source) {
                  ++num_outputs;
                  return consuming ? std::make_unique<wiola::testing::FakeOutput>(source)
                                   : std::make_unique<wiola::testing::FakeOutput>();
              },
              std::make_shared<wiola::testing::FakeMediaProvider>(
                  [this](const std::filesystem::path& path,
                      const wiola::media::OperationContext&) -> wiola::media::DecoderOpenResult {
                      const auto info{lookup(path)};
                      if (!info)
                          return missing();
                      return wiola::media::DecoderOpenResult{
                          std::make_unique<wiola::testing::FakeDecoder>(*info)};
                  },
                  [this](const std::filesystem::path& path)
                      -> wiola::media::Result<wiola::media::Metadata> {
                      const auto info{lookup(path)};
                      if (!info)
                          return missing();
                      wiola::media::Metadata result;
                      result.track.duration = info->spec.time_per(info->length.value());
                      return result;
                  }
              )}
    {
    }

    std::filesystem::path track(wiola::pcm::StreamSpec spec = {})
    {
        const std::filesystem::path path{"resource-" + std::to_string(++next_resource_) + ".media"};
        const wiola::media::DecoderStreamInfo info{.spec = spec,
            .length = spec.checked_frames_at(duration).value(),
            .length_accuracy = wiola::media::LengthAccuracy::exact,
            .seekable = true};
        const std::lock_guard lock{media_mutex_};
        media_.emplace(path, info);
        return path;
    }

    /// Set before loading, when a test needs a track that cannot run out from under it.
    bool consuming{true};
    std::size_t num_outputs{0};
    wiola::testing::Shaping processing{wiola::pcm::StreamSpec{}};
    Session session;
};

/// A supplied processing implementation independent of the built-in equalizer and topology.
class SuppliedProcessing final : public wiola::audio::Stage {
public:
    std::unique_ptr<Preparation> prepare(wiola::pcm::StreamSpec spec) noexcept override
    {
        prepared_for = spec;
        return nullptr;
    }

    void process(std::span<float> samples) noexcept override
    {
        for (auto& sample : samples)
            sample += 0.25F;
    }

    std::optional<wiola::pcm::StreamSpec> prepared_for;
};

TEST(Session, PreparesAndRendersThroughTheHostSuppliedProcessingPort)
{
    SuppliedProcessing processing;
    wiola::audio::Source* rendered{};
    wiola::testing::FakeOutput* device{};
    Session session{processing,
        [&](wiola::audio::Source& source) {
            rendered = &source;
            auto output{std::make_unique<wiola::testing::FakeOutput>()};
            device = output.get();
            return output;
        },
        std::make_shared<wiola::testing::FakeMediaProvider>(
            [](const std::filesystem::path&, const wiola::media::OperationContext&) {
                return wiola::media::DecoderOpenResult{
                    std::make_unique<wiola::testing::FakeDecoder>()};
            },
            [](const std::filesystem::path&) { return wiola::media::Metadata{}; }
        )};
    ASSERT_EQ(load_and_wait(session, "supplied-processing"), OpenResult::opened);
    ASSERT_TRUE(processing.prepared_for);
    EXPECT_EQ(*processing.prepared_for, wiola::pcm::StreamSpec{});
    ASSERT_TRUE(session.play());
    ASSERT_TRUE(poll_until(session, [&] { return device->running(); }));
    std::array<float, 2> block{};
    EXPECT_EQ(rendered->render(block), block.size());
    EXPECT_EQ(block, (std::array{0.25F, 0.25F}));
}

TEST(Session, StartsWithNothingLoaded)
{
    const Fixture fixture;

    EXPECT_FALSE(fixture.session.loaded());
    EXPECT_EQ(fixture.session.state(), State::idle);
    EXPECT_FALSE(fixture.session.playing());
    EXPECT_EQ(fixture.session.total_time(), std::nullopt);
    EXPECT_EQ(fixture.session.time_played(), units::Time{});
}

/// Nothing loaded is not a reason to refuse to be asked.
TEST(Session, AnswersWhileNothingIsLoaded)
{
    Fixture fixture;

    EXPECT_FALSE(fixture.session.play());
    EXPECT_FALSE(fixture.session.pause());
    EXPECT_FALSE(fixture.session.resume());

    fixture.session.stop();
    fixture.session.seek(units::Time{1.0});

    EXPECT_FALSE(fixture.session.loaded());
    EXPECT_EQ(fixture.num_outputs, 0U);
}

TEST(Session, LoadsATrack)
{
    Fixture fixture;

    ASSERT_EQ(load_and_wait(fixture.session, fixture.track()), OpenResult::opened);

    EXPECT_TRUE(fixture.session.loaded());
    EXPECT_EQ(fixture.num_outputs, 1U);
    ASSERT_TRUE(fixture.session.total_time());
    EXPECT_EQ(*fixture.session.total_time(), Fixture::duration);
}

TEST(Session, RejectsAnInvalidStreamBeforeBuildingAPipeline)
{
    std::size_t num_outputs{0};
    wiola::audio::ProcessingChain processing;
    Session session{processing,
        [&num_outputs](wiola::audio::Source&) {
            ++num_outputs;
            return std::make_unique<wiola::testing::FakeOutput>();
        },
        std::make_shared<wiola::testing::FakeMediaProvider>(
            [](const std::filesystem::path&, const wiola::media::OperationContext&) {
                auto decoder{std::make_unique<wiola::testing::FakeDecoder>(Frames{64},
                    wiola::pcm::StreamSpec{.sample_rate = units::Frequency{48000.0},
                        .num_channels = 0})};
                return wiola::media::DecoderOpenResult{std::move(decoder)};
            },
            [](const std::filesystem::path&) { return wiola::media::Metadata{}; }
        )};

    EXPECT_EQ(session.open({"invalid.wav"}), OpenResult::loading);
    while (session.reading())
        std::this_thread::yield();
    session.catch_up();

    EXPECT_EQ(session.open_result(), OpenResult::damaged);
    EXPECT_FALSE(session.loaded());
    EXPECT_EQ(num_outputs, 0U);
}

TEST(Session, OwnsIndependentAndValidatedBufferingConfiguration)
{
    const auto loads_with = [](units::Time duration) {
        wiola::audio::ProcessingChain processing;
        Session session{processing,
            [](wiola::audio::Source&) { return std::make_unique<wiola::testing::FakeOutput>(); },
            std::make_shared<wiola::testing::FakeMediaProvider>(
                [](const std::filesystem::path&, const wiola::media::OperationContext&) {
                    return wiola::media::DecoderOpenResult{
                        std::make_unique<wiola::testing::FakeDecoder>()};
                },
                [](const std::filesystem::path&) { return wiola::media::Metadata{}; }
            ),
            wiola::engine::TransportConfig{}, wiola::engine::BufferingConfig{.duration = duration}};

        session.open({"track.wav"});
        while (session.reading())
            std::this_thread::yield();
        session.catch_up();

        return session.loaded();
    };

    EXPECT_TRUE(loads_with(10_ms));
    EXPECT_TRUE(loads_with(20_ms));
    EXPECT_THROW(loads_with(units::Time{}), std::invalid_argument);
}

TEST(Session, RejectsInvalidConstructionBeforeStartingAnyCollaborator)
{
    wiola::audio::ProcessingChain processing;
    std::atomic<unsigned> media_calls{};
    unsigned output_calls{};
    const auto provider{std::make_shared<wiola::testing::FakeMediaProvider>(
        [&](const std::filesystem::path&, const wiola::media::OperationContext&) {
            ++media_calls;
            return wiola::media::DecoderOpenResult{std::make_unique<wiola::testing::FakeDecoder>()};
        },
        [&](const std::filesystem::path&) {
            ++media_calls;
            return wiola::media::Metadata{};
        }
    )};
    const auto construct{
        [&](std::shared_ptr<wiola::media::MediaProvider> media,
            wiola::engine::TransportConfig transport, wiola::engine::BufferingConfig buffering) {
            return std::make_unique<Session>(
                processing,
                [&](wiola::audio::Source&) {
                    ++output_calls;
                    return std::make_unique<wiola::testing::FakeOutput>();
                },
                std::move(media), transport, buffering
            );
        }};
    EXPECT_THROW(construct({}, {}, {}), std::invalid_argument);
    wiola::engine::TransportConfig invalid;
    invalid.decode_chunk = wiola::pcm::Frames{};
    EXPECT_THROW(construct(provider, invalid, {}), std::invalid_argument);
    invalid = {};
    invalid.poll_interval = std::chrono::milliseconds{};
    EXPECT_THROW(construct(provider, invalid, {}), std::invalid_argument);
    EXPECT_THROW(construct(provider, {}, wiola::engine::BufferingConfig{.duration = units::Time{}}),
        std::invalid_argument);
    EXPECT_EQ(media_calls, 0U);
    EXPECT_EQ(output_calls, 0U);
}

TEST(Session, RefusesAFileItCannotRead)
{
    Fixture fixture;

    EXPECT_EQ(load_and_wait(fixture.session, std::filesystem::path{"no-such-track.wav"}),
        OpenResult::unreadable);
    EXPECT_FALSE(fixture.session.loaded());
    EXPECT_EQ(fixture.num_outputs, 0U);
}

TEST(Session, NamesNoTrackBeforeOneIsLoaded)
{
    const Fixture fixture;

    EXPECT_TRUE(fixture.session.track().empty());
}

TEST(Session, NamesTheTrackItLoaded)
{
    Fixture fixture;

    const std::filesystem::path path{fixture.track()};

    ASSERT_EQ(load_and_wait(fixture.session, path), OpenResult::opened);

    EXPECT_EQ(fixture.session.track(), path);
}

/// The name follows what is playing, not what was asked for: a file that would not open leaves
/// both alone.
TEST(Session, KeepsTheNameOfTheTrackAFailedLoadDidNotReplace)
{
    Fixture fixture;

    const std::filesystem::path path{fixture.track()};

    ASSERT_EQ(load_and_wait(fixture.session, path), OpenResult::opened);
    ASSERT_EQ(load_and_wait(fixture.session, std::filesystem::path{"no-such-track.wav"}),
        OpenResult::unreadable);

    EXPECT_EQ(fixture.session.track(), path);
}

TEST(Session, KeepsWhatWasLoadedWhenTheNextFileFails)
{
    Fixture fixture;

    // The track must still be playing when this test asks, so the output plays none of it: one
    // that drains at the speed it is fed runs a track out in the time these lines take.
    fixture.consuming = false;

    ASSERT_EQ(load_and_wait(fixture.session, fixture.track()), OpenResult::opened);
    ASSERT_TRUE(fixture.session.play());
    ASSERT_EQ(fixture.session.state(), State::playing);

    EXPECT_EQ(load_and_wait(fixture.session, std::filesystem::path{"no-such-track.wav"}),
        OpenResult::unreadable);

    EXPECT_TRUE(fixture.session.loaded());
    EXPECT_EQ(fixture.session.state(), State::playing);
}

/// Asking says nothing about the answer: what was playing is still playing until a file has been
/// read all the way through.
TEST(Session, KeepsPlayingWhileTheNextFileIsRead)
{
    Fixture fixture;

    // The track must still be playing when this test asks, so the output plays none of it: one
    // that drains at the speed it is fed runs a track out in the time these lines take.
    fixture.consuming = false;

    ASSERT_EQ(load_and_wait(fixture.session, fixture.track()), OpenResult::opened);
    ASSERT_TRUE(fixture.session.play());

    EXPECT_EQ(fixture.session.open({fixture.track()}), OpenResult::loading);
    EXPECT_EQ(fixture.session.state(), State::playing);
    EXPECT_TRUE(fixture.session.loaded());

    while (fixture.session.reading())
        std::this_thread::yield();

    fixture.session.catch_up();

    EXPECT_EQ(fixture.session.open_result(), OpenResult::opened);
    EXPECT_EQ(fixture.session.state(), State::idle) << "the new track is loaded, not playing";
}

/// A device is opened for a format, and a track in the same one plays through the device that is
/// already open: a track change that closed and opened a device would be heard.
TEST(Session, KeepsTheDeviceAcrossTracksOfTheSameFormat)
{
    Fixture fixture;
    const std::filesystem::path path{fixture.track()};

    ASSERT_EQ(load_and_wait(fixture.session, path), OpenResult::opened);
    ASSERT_EQ(load_and_wait(fixture.session, path), OpenResult::opened);

    EXPECT_EQ(fixture.num_outputs, 1U);
}

/// A format it cannot play is a device it has to open again: what is open was cut for the rate
/// of the track before.
TEST(Session, OpensAnotherDeviceWhenTheFormatChanges)
{
    Fixture fixture;

    ASSERT_EQ(load_and_wait(fixture.session, fixture.track()), OpenResult::opened);
    ASSERT_EQ(fixture.num_outputs, 1U);

    ASSERT_EQ(load_and_wait(fixture.session,
                  fixture.track(wiola::pcm::StreamSpec{.sample_rate = units::Frequency{44100.0},
                      .num_channels = 2})),
        OpenResult::opened);

    EXPECT_EQ(fixture.num_outputs, 2U);
}

TEST(Session, PlaysAndPauses)
{
    Fixture fixture;

    ASSERT_EQ(load_and_wait(fixture.session, fixture.track()), OpenResult::opened);

    ASSERT_TRUE(fixture.session.play());
    EXPECT_EQ(fixture.session.state(), State::playing);
    EXPECT_TRUE(fixture.session.playing());

    ASSERT_TRUE(fixture.session.pause());
    EXPECT_EQ(fixture.session.state(), State::paused);

    ASSERT_TRUE(fixture.session.resume());
    EXPECT_EQ(fixture.session.state(), State::playing);
}

TEST(Session, StopsAtTheBeginning)
{
    Fixture fixture;

    ASSERT_EQ(load_and_wait(fixture.session, fixture.track()), OpenResult::opened);
    ASSERT_TRUE(fixture.session.play());

    fixture.session.seek(units::Time{1.0});
    fixture.session.stop();

    EXPECT_EQ(fixture.session.state(), State::stopped);
    EXPECT_EQ(fixture.session.time_played(), units::Time{});
}

TEST(Session, SeeksWhereItIsAsked)
{
    Fixture fixture;

    ASSERT_EQ(load_and_wait(fixture.session, fixture.track()), OpenResult::opened);

    fixture.session.seek(units::Time{1.0});

    EXPECT_NEAR(fixture.session.time_played().get<units::Sec>(), 1.0, 0.01);
}

/// A setting belongs to the listener, not to what is playing.
TEST(Session, PreservesSuppliedProcessingSettingsAcrossTracks)
{
    Fixture fixture;
    const std::filesystem::path path{fixture.track()};

    fixture.processing.volume.set_gain(0.25F);
    fixture.processing.equalizer.set_band_gain(3, 6.0F);

    ASSERT_EQ(load_and_wait(fixture.session, path), OpenResult::opened);

    EXPECT_FLOAT_EQ(fixture.processing.volume.gain(), 0.25F);
    EXPECT_FLOAT_EQ(fixture.processing.equalizer.band_gain(3), 6.0F);
}

} // namespace

/// The point of a list: a track running out is the cue to play the next one.
TEST(Session, PlaysTheNextTrackWhenOneEnds)
{
    Fixture fixture;

    const std::filesystem::path first{fixture.track()};
    const std::filesystem::path next{fixture.track()};

    ASSERT_EQ(fixture.session.open({first, next}), OpenResult::loading);
    ASSERT_TRUE(poll_until(fixture.session, [&fixture] { return fixture.session.loaded(); }));
    ASSERT_TRUE(fixture.session.play());

    EXPECT_TRUE(poll_until(fixture.session, [&fixture, &next] {
        return fixture.session.track() == next;
    })) << "the next track was never played";
    EXPECT_TRUE(poll_until(fixture.session, [&fixture] { return fixture.session.playing(); }));
}

/// The end of the list is the end of playback: it stays where it stopped.
TEST(Session, StaysAtTheEndOfTheList)
{
    Fixture fixture;

    const std::filesystem::path only{fixture.track()};

    ASSERT_EQ(load_and_wait(fixture.session, only), OpenResult::opened);
    ASSERT_TRUE(fixture.session.play());

    ASSERT_TRUE(poll_until(fixture.session,
        [&fixture] { return fixture.session.state() == State::ended; }));

    fixture.session.catch_up();

    EXPECT_FALSE(fixture.session.reading()) << "there was nowhere to go, and it went";
    EXPECT_EQ(fixture.session.state(), State::ended);
    EXPECT_EQ(fixture.session.track(), only);
}

/// A listener who pressed stop did not ask for the next track.
TEST(Session, PlaysNothingNextWhenTheListenerStops)
{
    Fixture fixture;
    fixture.consuming = false;

    const std::filesystem::path first{fixture.track()};
    const std::filesystem::path next{fixture.track()};

    ASSERT_EQ(fixture.session.open({first, next}), OpenResult::loading);
    ASSERT_TRUE(poll_until(fixture.session, [&fixture] { return fixture.session.loaded(); }));
    ASSERT_TRUE(fixture.session.play());

    fixture.session.stop();
    fixture.session.catch_up();

    // Nothing was even begun: waiting to see whether a track arrives would pass whether or not
    // one was on its way.
    EXPECT_FALSE(fixture.session.reading())
        << "a listener's stop was taken as a cue to read the next track";

    ASSERT_TRUE(poll_until(fixture.session, [&fixture] { return !fixture.session.reading(); }));

    EXPECT_EQ(fixture.session.track(), first);
    EXPECT_EQ(fixture.session.state(), State::stopped);
}

TEST(Session, PlaysTheNextTrackAndTheOneBefore)
{
    Fixture fixture;
    fixture.consuming = false;

    const std::filesystem::path first{fixture.track()};
    const std::filesystem::path next{fixture.track()};

    ASSERT_EQ(fixture.session.open({first, next}), OpenResult::loading);
    ASSERT_TRUE(poll_until(fixture.session, [&fixture] { return fixture.session.loaded(); }));

    ASSERT_TRUE(fixture.session.next_track());
    ASSERT_TRUE(poll_until(fixture.session,
        [&fixture, &next] { return fixture.session.track() == next; }));

    ASSERT_TRUE(fixture.session.previous_track());
    EXPECT_TRUE(poll_until(fixture.session,
        [&fixture, &first] { return fixture.session.track() == first; }));
}

/// Adding is for the queue, not for what is on: a track playing keeps playing.
TEST(Session, AddsToTheQueueWithoutDisturbingWhatIsPlaying)
{
    Fixture fixture;
    fixture.consuming = false;

    const std::filesystem::path first{fixture.track()};
    const std::filesystem::path added{fixture.track()};

    ASSERT_EQ(load_and_wait(fixture.session, first), OpenResult::opened);
    ASSERT_TRUE(fixture.session.play());

    fixture.session.add({added});

    EXPECT_EQ(fixture.session.playlist().tracks().size(), 2U);
    EXPECT_EQ(fixture.session.track(), first);
    EXPECT_EQ(fixture.session.state(), State::playing);
}

/// What is added is read too, and every one of them.
TEST(Session, TakesUpWhatEveryAddedTrackSays)
{
    Fixture fixture;

    const std::vector<std::filesystem::path> both{fixture.track(), fixture.track()};

    fixture.session.add(both);

    EXPECT_TRUE(poll_until(fixture.session, [&fixture] {
        const auto queued{fixture.session.playlist().tracks()};

        return queued.size() == 2 && queued.front().metadata().duration.has_value() &&
            queued.back().metadata().duration.has_value();
    })) << "a track that was added was never read";
}

/// Adding to an empty queue is the first thing a listener does, and they mean to play it.
TEST(Session, ReadsTheFirstTrackAddedToAnEmptyQueue)
{
    Fixture fixture;

    const std::filesystem::path added{fixture.track()};

    fixture.session.add({added});

    ASSERT_TRUE(poll_until(fixture.session, [&fixture] { return fixture.session.loaded(); }));
    EXPECT_EQ(fixture.session.track(), added);
    EXPECT_EQ(fixture.session.state(), State::idle);
}

/// The queue says what comes next, not what is on: taking out the track that is playing leaves
/// it playing.
TEST(Session, KeepsPlayingATrackTakenOutOfTheQueue)
{
    Fixture fixture;
    fixture.consuming = false;

    const std::filesystem::path first{fixture.track()};
    const std::filesystem::path next{fixture.track()};

    ASSERT_EQ(fixture.session.open({first, next}), OpenResult::loading);
    ASSERT_TRUE(poll_until(fixture.session, [&fixture] { return fixture.session.loaded(); }));
    ASSERT_TRUE(fixture.session.play());

    EXPECT_TRUE(fixture.session.remove(0));

    EXPECT_EQ(fixture.session.playlist().tracks().size(), 1U);
    EXPECT_EQ(fixture.session.track(), first);
    EXPECT_EQ(fixture.session.state(), State::playing);
    EXPECT_FALSE(fixture.session.remove(1));
}

TEST(Session, EmptiesTheQueueAndEndsPlayback)
{
    Fixture fixture;
    fixture.consuming = false;

    ASSERT_EQ(fixture.session.open({fixture.track(), fixture.track()}), OpenResult::loading);
    ASSERT_TRUE(poll_until(fixture.session, [&fixture] { return fixture.session.loaded(); }));
    ASSERT_TRUE(fixture.session.play());

    fixture.session.clear();

    EXPECT_TRUE(fixture.session.playlist().empty());
    EXPECT_EQ(fixture.session.state(), State::stopped);

    // What was playing is still loaded, so it can be played again without opening it afresh.
    EXPECT_TRUE(fixture.session.loaded());
}

/// A queue shows what its tracks say about themselves, which is read off the thread that queued
/// them and taken up as it arrives.
TEST(Session, TakesUpWhatQueuedTracksSayAboutThemselves)
{
    Fixture fixture;

    const std::filesystem::path track{fixture.track()};

    ASSERT_EQ(fixture.session.open({track}), OpenResult::loading);

    ASSERT_TRUE(poll_until(fixture.session, [&fixture] {
        return fixture.session.playlist().tracks().front().metadata().duration.has_value();
    })) << "how long the track runs was never found out";

    const wiola::engine::QueueEntry& described{fixture.session.playlist().tracks().front()};

    ASSERT_TRUE(described.metadata().duration.has_value());
    EXPECT_GT(*described.metadata().duration, units::Time{});
    EXPECT_FALSE(described.metadata().title.empty());
}

/// Every track of a list is read, not only the first: opening several is the one way a listener
/// fills a queue in one go.
TEST(Session, TakesUpWhatEveryTrackOfAnOpenedListSays)
{
    Fixture fixture;

    const std::filesystem::path first{fixture.track()};
    const std::filesystem::path next{fixture.track()};
    const std::vector<std::filesystem::path> both{first, next};

    ASSERT_EQ(fixture.session.open(both), OpenResult::loading);

    ASSERT_TRUE(poll_until(fixture.session, [&fixture] {
        const auto queued{fixture.session.playlist().tracks()};

        return queued.size() == 2 && queued.front().metadata().duration.has_value() &&
            queued.back().metadata().duration.has_value();
    })) << "a track of the list was never read";
}

/// Until then it is a track with the file's own name, rather than a row waiting for something.
TEST(Session, NamesAQueuedTrackAfterItsFileToBeginWith)
{
    Fixture fixture;

    const std::filesystem::path track{fixture.track()};

    ASSERT_EQ(fixture.session.open({track}), OpenResult::loading);

    EXPECT_EQ(fixture.session.playlist().tracks().front().metadata().title, track.stem().string());
}

/// What a listener picking a track out of their queue asks for.
TEST(Session, PlaysTheTrackItIsSentTo)
{
    Fixture fixture;
    fixture.consuming = false;

    const std::filesystem::path first{fixture.track()};
    const std::filesystem::path next{fixture.track()};

    ASSERT_EQ(fixture.session.open({first, next}), OpenResult::loading);
    ASSERT_TRUE(poll_until(fixture.session, [&fixture] { return fixture.session.loaded(); }));

    ASSERT_TRUE(fixture.session.play_track(1));
    EXPECT_TRUE(poll_until(fixture.session,
        [&fixture, &next] { return fixture.session.track() == next; }));

    EXPECT_EQ(fixture.session.playlist().position(), 1U);
    EXPECT_FALSE(fixture.session.play_track(2)) << "there is no third track to be sent to";
}

/// The queue is what was given, in the order it was given: shuffling changes what plays next,
/// not the list a listener is looking at.
TEST(Session, ShowsTheQueueInTheOrderItWasGiven)
{
    Fixture fixture;

    const std::filesystem::path first{fixture.track()};
    const std::filesystem::path next{fixture.track()};

    ASSERT_EQ(fixture.session.open({first, next}), OpenResult::loading);
    fixture.session.shuffle(true);
    EXPECT_TRUE(fixture.session.shuffled());

    ASSERT_EQ(fixture.session.playlist().tracks().size(), 2U);
    EXPECT_EQ(fixture.session.playlist().tracks().front().path(), first);
    EXPECT_EQ(fixture.session.playlist().tracks().back().path(), next);
    fixture.session.shuffle(false);
    EXPECT_FALSE(fixture.session.shuffled());
    EXPECT_EQ(fixture.session.playlist().tracks().front().path(), first);
    EXPECT_EQ(fixture.session.playlist().tracks().back().path(), next);
}

TEST(Session, GoesNowhereBeyondTheEndsOfTheList)
{
    Fixture fixture;

    ASSERT_EQ(load_and_wait(fixture.session, fixture.track()), OpenResult::opened);

    EXPECT_FALSE(fixture.session.next_track());
    EXPECT_FALSE(fixture.session.previous_track());
}

/// Skipping while a track plays plays the one skipped to; skipping while nothing does, does not.
TEST(Session, KeepsPlayingOrNotWhenTheTrackChanges)
{
    Fixture fixture;
    fixture.consuming = false;

    const std::filesystem::path first{fixture.track()};
    const std::filesystem::path next{fixture.track()};

    ASSERT_EQ(fixture.session.open({first, next}), OpenResult::loading);
    ASSERT_TRUE(poll_until(fixture.session, [&fixture] { return fixture.session.loaded(); }));
    ASSERT_TRUE(fixture.session.play());

    ASSERT_TRUE(fixture.session.next_track());
    EXPECT_TRUE(poll_until(fixture.session, [&fixture] { return fixture.session.playing(); }));

    fixture.session.stop();
    ASSERT_TRUE(fixture.session.previous_track());
    ASSERT_TRUE(poll_until(fixture.session,
        [&fixture, &first] { return fixture.session.track() == first; }));

    EXPECT_FALSE(fixture.session.playing());
}

TEST(Session, EnrichesAQueueLargerThanTheMetadataWorkerCapacity)
{
    std::atomic<std::size_t> num_reads{0};
    wiola::audio::ProcessingChain processing;
    Session session{processing,
        [](wiola::audio::Source&) { return std::make_unique<wiola::testing::FakeOutput>(); },
        std::make_shared<wiola::testing::FakeMediaProvider>(
            [](const std::filesystem::path&, const wiola::media::OperationContext&) {
                return wiola::media::DecoderOpenResult{
                    std::make_unique<wiola::testing::FakeDecoder>()};
            },
            [&num_reads](const std::filesystem::path&) {
                ++num_reads;
                wiola::media::Metadata metadata;
                metadata.track.title = "read";
                return metadata;
            }
        )};
    std::vector<std::filesystem::path> paths;
    paths.reserve(300);

    for (std::size_t index{0}; index < 300; ++index)
        paths.push_back("track-" + std::to_string(index) + ".wav");

    EXPECT_EQ(session.open(std::move(paths)), OpenResult::loading);
    ASSERT_TRUE(poll_until(session,
        [&session] { return session.playlist().metadata_revision() == 300U; }));

    EXPECT_EQ(num_reads, 300U);
    for (const auto& track : session.playlist().tracks())
        EXPECT_EQ(track.metadata().title, "read");
}

TEST(Session, MetadataFailureLeavesPlayableAudioAndFallbackTitleIntact)
{
    wiola::audio::ProcessingChain processing;
    Session session{processing,
        [](wiola::audio::Source&) { return std::make_unique<wiola::testing::FakeOutput>(); },
        std::make_shared<wiola::testing::FakeMediaProvider>(
            [](const std::filesystem::path&, const wiola::media::OperationContext&) {
                return wiola::media::DecoderOpenResult{
                    std::make_unique<wiola::testing::FakeDecoder>()};
            },
            [](const std::filesystem::path&) -> wiola::media::Result<wiola::media::Metadata> {
                return wiola::media::unexpected{
                    wiola::media::Error{.category = wiola::media::ErrorCategory::unsupported,
                                        .detail = "no tag format"}
                };
            }
        )};

    EXPECT_EQ(session.open({"playable.wav"}), OpenResult::loading);
    ASSERT_TRUE(poll_until(session,
        [&session] { return session.loaded() && session.playlist().metadata_revision() == 1U; }));

    EXPECT_EQ(session.open_result(), OpenResult::opened);
    EXPECT_TRUE(session.loaded());
    EXPECT_EQ(session.playlist().tracks().front().metadata().title, "playable");
}

TEST(Session, ClearDuringLoadPreventsAnObsoleteInstall)
{
    wiola::testing::Gate opening;
    wiola::audio::ProcessingChain processing;
    Session session{processing,
        [](wiola::audio::Source&) { return std::make_unique<wiola::testing::FakeOutput>(); },
        std::make_shared<wiola::testing::FakeMediaProvider>(
            [&opening](const std::filesystem::path&, const wiola::media::OperationContext&) {
                opening.wait();
                return wiola::media::DecoderOpenResult{
                    std::make_unique<wiola::testing::FakeDecoder>()};
            },
            [](const std::filesystem::path&) { return wiola::media::Metadata{}; }
        )};

    session.open({"late.wav"});
    opening.wait_until_entered();
    session.clear();
    opening.release();

    ASSERT_TRUE(poll_until(session, [&session] { return !session.reading(); }));
    session.catch_up();

    EXPECT_FALSE(session.loaded());
    EXPECT_EQ(session.state(), State::idle);
}

TEST(Session, StopDuringLoadPreventsAnObsoleteAutoplay)
{
    wiola::testing::Gate opening;
    const std::filesystem::path second{"second.wav"};
    wiola::audio::ProcessingChain processing;
    Session session{processing,
        [](wiola::audio::Source&) { return std::make_unique<wiola::testing::FakeOutput>(); },
        std::make_shared<wiola::testing::FakeMediaProvider>(
            [&opening, &second](const std::filesystem::path& path,
                const wiola::media::OperationContext&) {
                if (path == second)
                    opening.wait();
                return wiola::media::DecoderOpenResult{
                    std::make_unique<wiola::testing::FakeDecoder>()};
            },
            [](const std::filesystem::path&) { return wiola::media::Metadata{}; }
        )};

    session.open({"first.wav", second});
    ASSERT_TRUE(poll_until(session, [&session] { return session.loaded(); }));
    ASSERT_TRUE(session.play());
    ASSERT_TRUE(session.next_track());
    opening.wait_until_entered();

    session.stop();
    opening.release();
    ASSERT_TRUE(poll_until(session, [&session] { return !session.reading(); }));
    session.catch_up();

    EXPECT_EQ(session.track(), std::filesystem::path{"first.wav"});
    EXPECT_EQ(session.state(), State::stopped);
    EXPECT_FALSE(session.playing());
}

TEST(Session, StopCancelsTheActiveOpenWithoutInstallingItsLateResult)
{
    wiola::testing::Gate opening;
    std::atomic<bool> cancelled{false};
    wiola::audio::ProcessingChain processing;
    Session session{processing, wiola::testing::passive_output,
        std::make_shared<wiola::testing::FakeMediaProvider>(
            [&](const std::filesystem::path&, const wiola::media::OperationContext& context) {
                std::stop_callback callback{context.stop, [&] { cancelled = true; }};
                opening.wait();
                return wiola::media::DecoderOpenResult{
                    std::make_unique<wiola::testing::FakeDecoder>()};
            },
            [](const std::filesystem::path&) { return wiola::media::Metadata{}; }
        )};
    ASSERT_EQ(session.open({"late.wav"}), OpenResult::loading);
    opening.wait_until_entered();
    session.stop();
    EXPECT_TRUE(cancelled);
    opening.release();
    ASSERT_TRUE(poll_until(session, [&] { return !session.reading(); }));
    EXPECT_FALSE(session.loaded());
}

TEST(Session, OpeningAnEmptyQueueCancelsAnObsoleteOpen)
{
    wiola::testing::Gate opening;
    std::atomic<bool> cancelled{false};
    wiola::audio::ProcessingChain processing;
    Session session{processing, wiola::testing::passive_output,
        std::make_shared<wiola::testing::FakeMediaProvider>(
            [&](const std::filesystem::path&, const wiola::media::OperationContext& context) {
                std::stop_callback callback{context.stop, [&] { cancelled = true; }};
                opening.wait();
                return wiola::media::DecoderOpenResult{
                    std::make_unique<wiola::testing::FakeDecoder>()};
            },
            [](const std::filesystem::path&) { return wiola::media::Metadata{}; }
        )};
    session.open({"late.wav"});
    opening.wait_until_entered();
    session.open({});
    EXPECT_TRUE(cancelled);
    opening.release();
    ASSERT_TRUE(poll_until(session, [&] { return !session.reading(); }));
    EXPECT_FALSE(session.loaded());
    EXPECT_TRUE(session.playlist().empty());
}

TEST(Session, RemovingThePendingOccurrenceCancelsItsOpenWithoutReplacingActivePlayback)
{
    wiola::testing::Gate opening;
    std::atomic<bool> cancelled{false};
    wiola::audio::ProcessingChain processing;
    Session session{processing, wiola::testing::passive_output,
        std::make_shared<wiola::testing::FakeMediaProvider>(
            [&](const std::filesystem::path& path, const wiola::media::OperationContext& context) {
                if (path == "pending.media") {
                    std::stop_callback callback{context.stop, [&] { cancelled = true; }};
                    opening.wait();
                }
                return wiola::media::DecoderOpenResult{
                    std::make_unique<wiola::testing::FakeDecoder>()};
            },
            [](const std::filesystem::path&) { return wiola::media::Metadata{}; }
        )};
    ASSERT_EQ(load_and_wait(session, "active.media"), OpenResult::opened);
    ASSERT_TRUE(session.play());
    ASSERT_EQ(session.open({"pending.media"}), OpenResult::loading);
    opening.wait_until_entered();

    EXPECT_TRUE(session.remove(0));
    EXPECT_TRUE(cancelled);
    opening.release();
    ASSERT_TRUE(poll_until(session, [&] { return !session.reading(); }));

    EXPECT_TRUE(session.playlist().empty());
    EXPECT_TRUE(session.loaded());
    EXPECT_TRUE(session.playing());
    EXPECT_EQ(session.track(), "active.media");
}

TEST(Session, ReplacementCommandsKeepTheLatestIntentAndStopResetsItsPosition)
{
    Fixture fixture;
    fixture.consuming = false;
    const auto path{fixture.track()};
    EXPECT_FALSE(fixture.session.release_media(path));
    ASSERT_EQ(load_and_wait(fixture.session, path), OpenResult::opened);
    EXPECT_FALSE(fixture.session.release_media("unrelated.media"));
    const auto handoff{fixture.session.release_media(path)};
    ASSERT_TRUE(handoff);

    EXPECT_TRUE(fixture.session.play());
    const auto playing{fixture.session.release_media(path)};
    ASSERT_TRUE(playing);
    EXPECT_EQ(playing->generation, handoff->generation);
    EXPECT_TRUE(playing->play);
    EXPECT_TRUE(fixture.session.pause());
    const auto paused{fixture.session.release_media(path)};
    ASSERT_TRUE(paused);
    EXPECT_FALSE(paused->play);
    EXPECT_TRUE(fixture.session.resume());
    fixture.session.seek(units::Time{0.4});
    const auto resumed{fixture.session.release_media(path)};
    ASSERT_TRUE(resumed);
    EXPECT_TRUE(resumed->play);
    EXPECT_EQ(resumed->position, units::Time{0.4});

    fixture.session.stop();
    const auto stopped{fixture.session.release_media(path)};
    ASSERT_TRUE(stopped);
    EXPECT_FALSE(stopped->play);
    EXPECT_EQ(stopped->position, units::Time{});
    ASSERT_EQ(fixture.session.restore_media(*handoff), OpenResult::loading);
    ASSERT_TRUE(poll_until(fixture.session, [&] { return fixture.session.loaded(); }));
    EXPECT_FALSE(fixture.session.playing());
    EXPECT_EQ(fixture.session.time_played(), units::Time{});
}

TEST(Session, HandoffMatchesInstalledMediaAfterAQueueReplacementFails)
{
    Fixture fixture;
    fixture.consuming = false;
    const auto path{fixture.track()};
    ASSERT_EQ(load_and_wait(fixture.session, path), OpenResult::opened);
    const auto installed{fixture.session.playlist().current()->id()};
    ASSERT_TRUE(fixture.session.play());
    fixture.session.seek(units::Time{0.4});
    ASSERT_EQ(load_and_wait(fixture.session, "missing.media"), OpenResult::unreadable);
    const auto selected{fixture.session.playlist().current()->id()};
    ASSERT_NE(installed, selected);

    EXPECT_FALSE(fixture.session.release_media("missing.media"));
    EXPECT_TRUE(fixture.session.loaded());
    EXPECT_TRUE(fixture.session.playing());
    const auto held{fixture.session.release_media(path)};
    ASSERT_TRUE(held);
    EXPECT_EQ(held->entry, installed);
    EXPECT_FALSE(fixture.session.loaded());
    ASSERT_EQ(fixture.session.restore_media(*held), OpenResult::loading);
    ASSERT_TRUE(poll_until(fixture.session, [&] { return fixture.session.loaded(); }));

    EXPECT_EQ(fixture.session.track(), path);
    EXPECT_EQ(fixture.session.playlist().current()->id(), selected);
    EXPECT_EQ(fixture.session.playlist().current()->path(), "missing.media");
    EXPECT_TRUE(fixture.session.playing());
    EXPECT_NEAR(fixture.session.time_played().seconds(), 0.4, 0.001);
}

TEST(Session, RetainedMediaCanBeRestoredAfterItsQueueEntryIsRemovedOrCleared)
{
    enum class Change {
        remove,
        clear,
    };
    for (const auto change : {Change::remove, Change::clear}) {
        Fixture fixture;
        fixture.consuming = false;
        const auto path{fixture.track()};
        ASSERT_EQ(load_and_wait(fixture.session, path), OpenResult::opened);
        ASSERT_TRUE(fixture.session.play());
        if (change == Change::remove)
            ASSERT_TRUE(fixture.session.remove(0));
        else
            fixture.session.clear();
        ASSERT_TRUE(fixture.session.playlist().empty());
        const auto held{fixture.session.release_media(path)};
        ASSERT_TRUE(held);
        EXPECT_FALSE(fixture.session.loaded());
        ASSERT_EQ(fixture.session.restore_media(*held), OpenResult::loading);
        ASSERT_TRUE(poll_until(fixture.session, [&] { return fixture.session.loaded(); }));
        EXPECT_TRUE(fixture.session.playlist().empty());
        EXPECT_EQ(fixture.session.track(), path);
        EXPECT_EQ(fixture.session.playing(), change == Change::remove);
    }
}

TEST(Session, HandoffPreservesAnUnrelatedOpenAndRestoresOnlyAfterItsFailure)
{
    enum class Outcome {
        success,
        failure,
        invalid,
    };
    enum class Timing {
        before_completion,
        after_completion,
    };
    for (const auto outcome : {Outcome::success, Outcome::failure, Outcome::invalid}) {
        for (const auto timing : {Timing::before_completion, Timing::after_completion}) {
            wiola::testing::Gate opening;
            std::atomic<bool> cancelled{false};
            std::atomic<int> active_opens{0};
            wiola::audio::ProcessingChain processing;
            Session session{processing, wiola::testing::passive_output,
                std::make_shared<wiola::testing::FakeMediaProvider>(
                    [&](const std::filesystem::path& path,
                        const wiola::media::OperationContext& context)
                        -> wiola::media::DecoderOpenResult {
                        if (path == "candidate.media") {
                            std::stop_callback callback{context.stop, [&] {
                                                            cancelled = true;
                                                            opening.release();
                                                        }};
                            opening.wait();
                            if (outcome == Outcome::failure)
                                return wiola::media::unexpected{
                                    wiola::media::Error{wiola::media::ErrorCategory::unreadable,
                                                        "controlled failure"}
                                };
                            if (outcome == Outcome::invalid)
                                return wiola::media::DecoderOpenResult{
                                    std::make_unique<wiola::testing::FakeDecoder>(
                                        wiola::media::DecoderStreamInfo{.spec = {},
                                            .length = std::nullopt,
                                            .length_accuracy = wiola::media::LengthAccuracy::exact}
                                    )};
                        } else {
                            ++active_opens;
                        }
                        return wiola::media::DecoderOpenResult{
                            std::make_unique<wiola::testing::FakeDecoder>()};
                    },
                    [](const std::filesystem::path&) { return wiola::media::Metadata{}; }
                )};
            ASSERT_EQ(load_and_wait(session, "active.media"), OpenResult::opened);
            ASSERT_TRUE(session.play());
            session.seek(units::Time{0.01});
            session.add({"candidate.media"});
            ASSERT_TRUE(session.next_track());
            opening.wait_until_entered();
            const auto selected{session.playlist().current()->id()};
            const auto held{session.release_media("active.media")};
            ASSERT_TRUE(held);
            EXPECT_FALSE(cancelled);
            if (timing == Timing::before_completion) {
                EXPECT_EQ(session.restore_media(*held), OpenResult::loading);
                EXPECT_TRUE(session.reading());
                EXPECT_EQ(active_opens, 1);
            }
            opening.release();
            if (timing == Timing::after_completion) {
                ASSERT_TRUE(poll_until(session, [&] { return !session.reading(); }));
                static_cast<void>(session.restore_media(*held));
            }
            ASSERT_TRUE(poll_until(session,
                [&] { return session.loaded() && !session.reading(); }));

            EXPECT_FALSE(cancelled);
            EXPECT_EQ(session.playlist().current()->id(), selected);
            EXPECT_TRUE(session.playing());
            if (outcome == Outcome::success) {
                EXPECT_EQ(session.track(), "candidate.media");
                EXPECT_EQ(active_opens, 1);
            } else {
                EXPECT_EQ(session.track(), "active.media");
                EXPECT_EQ(active_opens, 2);
                EXPECT_NEAR(session.time_played().seconds(), 0.01, 0.001);
            }
            const auto request{session.open_request_id()};
            static_cast<void>(session.restore_media(*held));
            EXPECT_EQ(session.open_request_id(), request);
            EXPECT_FALSE(session.reading());
        }
    }
}

TEST(Session, HandoffCancelsASameResourceOpenButKeepsItsNewOccurrenceIntent)
{
    wiola::testing::Gate opening;
    std::atomic<int> opens{0};
    std::atomic<bool> cancelled{false};
    wiola::audio::ProcessingChain processing;
    Session session{processing, wiola::testing::passive_output,
        std::make_shared<wiola::testing::FakeMediaProvider>(
            [&](const std::filesystem::path&, const wiola::media::OperationContext& context) {
                if (++opens == 2) {
                    std::stop_callback callback{context.stop, [&] {
                                                    cancelled = true;
                                                    opening.release();
                                                }};
                    opening.wait();
                }
                return wiola::media::DecoderOpenResult{
                    std::make_unique<wiola::testing::FakeDecoder>()};
            },
            [](const std::filesystem::path&) { return wiola::media::Metadata{}; }
        )};
    ASSERT_EQ(session.open({"same.media", "same.media"}), OpenResult::loading);
    ASSERT_TRUE(poll_until(session, [&] { return session.loaded(); }));
    ASSERT_TRUE(session.play());
    session.seek(units::Time{0.01});
    ASSERT_TRUE(session.next_track());
    opening.wait_until_entered();
    const auto selected{session.playlist().current()->id()};

    const auto held{session.release_media("same.media")};
    ASSERT_TRUE(held);
    EXPECT_TRUE(cancelled);
    EXPECT_EQ(held->entry, selected);
    EXPECT_TRUE(held->play);
    EXPECT_EQ(held->position, units::Time{});
    ASSERT_EQ(session.restore_media(*held), OpenResult::loading);
    ASSERT_TRUE(poll_until(session, [&] { return session.loaded(); }));
    EXPECT_EQ(opens, 3);
    EXPECT_EQ(session.playlist().position(), 1U);
    EXPECT_TRUE(session.playing());
    EXPECT_EQ(session.time_played(), units::Time{});
}

TEST(Session, NewSelectionDuringHandoffKeepsPlayIntentAndRevokesTheOldTicket)
{
    Fixture fixture;
    fixture.consuming = false;
    const auto first{fixture.track()};
    const auto next{fixture.track()};
    ASSERT_EQ(load_and_wait(fixture.session, first), OpenResult::opened);
    fixture.session.add({next});
    ASSERT_TRUE(fixture.session.play());
    const auto held{fixture.session.release_media(first)};
    ASSERT_TRUE(held);
    ASSERT_TRUE(fixture.session.play_track(1));
    const auto request{fixture.session.open_request_id()};
    static_cast<void>(fixture.session.restore_media(*held));
    EXPECT_EQ(fixture.session.open_request_id(), request);
    ASSERT_TRUE(poll_until(fixture.session, [&] { return fixture.session.loaded(); }));
    EXPECT_EQ(fixture.session.track(), next);
    EXPECT_TRUE(fixture.session.playing());
}

TEST(Session, InvalidHandoffSeekTimesCannotPoisonTheRestorationRequest)
{
    Fixture fixture;
    fixture.consuming = false;
    const auto path{fixture.track()};
    ASSERT_EQ(load_and_wait(fixture.session, path), OpenResult::opened);
    const auto held{fixture.session.release_media(path)};
    ASSERT_TRUE(held);
    fixture.session.seek(units::Time{0.4});
    for (const double invalid : {std::numeric_limits<double>::quiet_NaN(),
             std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()}) {
        fixture.session.seek(units::Time{invalid});
        const auto current{fixture.session.release_media(path)};
        ASSERT_TRUE(current);
        EXPECT_EQ(current->position, units::Time{0.4});
    }
    fixture.session.seek(units::Time{-0.5});
    const auto current{fixture.session.release_media(path)};
    ASSERT_TRUE(current);
    EXPECT_EQ(current->position, units::Time{});
    ASSERT_EQ(fixture.session.restore_media(*held), OpenResult::loading);
    ASSERT_TRUE(poll_until(fixture.session, [&] { return fixture.session.loaded(); }));
    EXPECT_EQ(fixture.session.time_played(), units::Time{});
}

TEST(Session, EmptyQueueReplacementRevokesAnUnrestoredHandoff)
{
    Fixture fixture;
    const auto path{fixture.track()};
    ASSERT_EQ(load_and_wait(fixture.session, path), OpenResult::opened);
    const auto held{fixture.session.release_media(path)};
    ASSERT_TRUE(held);
    fixture.session.open({});
    static_cast<void>(fixture.session.restore_media(*held));
    EXPECT_TRUE(fixture.session.playlist().empty());
    EXPECT_FALSE(fixture.session.loaded());
    EXPECT_FALSE(fixture.session.reading());
    EXPECT_FALSE(fixture.session.play());
    EXPECT_FALSE(fixture.session.release_media(path));
}

TEST(Session, ReplacementHandoffQuiescesAndRestoresTheCurrentPlayIntent)
{
    Fixture fixture;
    fixture.consuming = false;
    const auto path = fixture.track();

    ASSERT_EQ(load_and_wait(fixture.session, path), OpenResult::opened);
    fixture.session.add({fixture.track()});
    ASSERT_TRUE(fixture.session.play());

    fixture.session.seek(units::Time{0.4});
    ASSERT_TRUE(poll_until(fixture.session,
        [&fixture] { return fixture.session.time_played().get<units::Sec>() >= 0.4; }));

    const auto handoff{fixture.session.release_media(path)};
    ASSERT_TRUE(handoff);
    EXPECT_FALSE(fixture.session.loaded());
    EXPECT_EQ(fixture.session.restore_media(*handoff), OpenResult::loading);
    EXPECT_FALSE(fixture.session.loaded());

    ASSERT_TRUE(poll_until(fixture.session, [&fixture] { return fixture.session.loaded(); }));

    EXPECT_EQ(fixture.session.track(), path);
    EXPECT_EQ(fixture.session.playlist().position(), 0U);
    EXPECT_EQ(fixture.session.state(), State::playing);
    EXPECT_EQ(fixture.num_outputs, 1U);
    EXPECT_NEAR(fixture.session.time_played().get<units::Sec>(), 0.4, 0.02);
}

TEST(Session, ReplacementHandoffHonorsNewerPauseSeekAndQueueClear)
{
    Fixture fixture;
    fixture.consuming = false;
    const auto path = fixture.track();
    ASSERT_EQ(load_and_wait(fixture.session, path), OpenResult::opened);
    ASSERT_TRUE(fixture.session.play());
    auto handoff = fixture.session.release_media(path);
    ASSERT_TRUE(handoff);
    EXPECT_FALSE(fixture.session.loaded());
    EXPECT_TRUE(fixture.session.pause());
    fixture.session.seek(units::Time{0.3});
    EXPECT_EQ(fixture.session.restore_media(*handoff), OpenResult::loading);
    ASSERT_TRUE(poll_until(fixture.session, [&] { return fixture.session.loaded(); }));
    EXPECT_FALSE(fixture.session.playing());
    EXPECT_NEAR(fixture.session.time_played().get<units::Sec>(), 0.3, 0.02);
    handoff = fixture.session.release_media(path);
    ASSERT_TRUE(handoff);
    fixture.session.clear();
    fixture.session.restore_media(*handoff);
    EXPECT_TRUE(fixture.session.playlist().tracks().empty());
    EXPECT_FALSE(fixture.session.play());
}

TEST(Session, ReplacementTicketsCannotRestoreANewerHandoffOrARemovedOccurrence)
{
    Fixture fixture;
    fixture.consuming = false;
    const auto path = fixture.track();
    ASSERT_EQ(load_and_wait(fixture.session, path), OpenResult::opened);
    const auto first = fixture.session.release_media(path);
    ASSERT_TRUE(first);
    ASSERT_EQ(fixture.session.restore_media(*first), OpenResult::loading);
    ASSERT_TRUE(poll_until(fixture.session, [&] { return fixture.session.loaded(); }));
    const auto second = fixture.session.release_media(path);
    ASSERT_TRUE(second);
    EXPECT_NE(first->generation, second->generation);
    EXPECT_EQ(fixture.session.restore_media(*first), OpenResult::opened);
    EXPECT_FALSE(fixture.session.reading());
    EXPECT_FALSE(fixture.session.loaded());
    ASSERT_TRUE(fixture.session.remove(0));
    EXPECT_FALSE(fixture.session.play());
    fixture.session.restore_media(*second);
    EXPECT_FALSE(fixture.session.reading());
    EXPECT_TRUE(fixture.session.playlist().empty());
}

TEST(Session, OutputSetupFailurePreservesActiveUnseekablePlayback)
{
    constexpr wiola::pcm::StreamSpec current_spec{.sample_rate = units::Frequency{48000.0},
        .num_channels = 2};
    constexpr wiola::pcm::StreamSpec next_spec{.sample_rate = units::Frequency{22050.0},
        .num_channels = 8};
    enum class Fault {
        no_output,
        exception,
        unknown_exception,
    };
    for (const auto fault : {Fault::no_output, Fault::exception, Fault::unknown_exception}) {
        wiola::testing::FakeOutput* active_device{};
        bool fail{false};
        wiola::testing::Shaping processing{wiola::pcm::StreamSpec{}};
        Session session{processing.chain,
            [&](wiola::audio::Source&) -> std::unique_ptr<wiola::audio::Output> {
                if (fail) {
                    if (fault == Fault::no_output)
                        return nullptr;
                    if (fault == Fault::exception)
                        throw std::runtime_error{"controlled device failure"};
                    throw fault;
                }
                auto output{std::make_unique<wiola::testing::FakeOutput>()};
                active_device = output.get();
                return output;
            },
            std::make_shared<wiola::testing::FakeMediaProvider>(
                [&](const std::filesystem::path& path,
                    const wiola::media::OperationContext&) -> wiola::media::DecoderOpenResult {
                    if (path == "current")
                        return wiola::media::DecoderOpenResult{
                            std::make_unique<wiola::testing::FakeDecoder>(
                                wiola::media::DecoderStreamInfo{.spec = current_spec,
                                    .length = std::nullopt,
                                    .seekable = false}
                            )};
                    return wiola::media::DecoderOpenResult{
                        std::make_unique<wiola::testing::FakeDecoder>(Frames{64}, next_spec)};
                },
                [](const std::filesystem::path&) { return wiola::media::Metadata{}; }
            )};
        ASSERT_EQ(load_and_wait(session, "current"), OpenResult::opened);
        processing.equalizer.set_band_gain(9, 3.0F);
        processing.volume.set_gain(0.25F);
        ASSERT_TRUE(session.play());
        ASSERT_TRUE(poll_until(session, [&] { return active_device->running(); }));
        active_device->play(Frames{32});
        const auto position{session.time_played()};
        static_cast<void>(session.take_outcomes());
        fail = true;
        ASSERT_EQ(session.open({"candidate"}), OpenResult::loading);
        const auto request{session.open_request_id()};
        while (session.reading())
            std::this_thread::yield();

        EXPECT_NO_THROW(session.catch_up());

        ASSERT_TRUE(session.loaded());
        EXPECT_EQ(session.open_result(), OpenResult::output_unavailable);
        EXPECT_EQ(session.track(), std::filesystem::path{"current"});
        EXPECT_TRUE(session.playing());
        EXPECT_FALSE(session.seekable());
        EXPECT_TRUE(active_device->running());
        EXPECT_EQ(session.time_played(), position);
        EXPECT_EQ(processing.equalizer.num_bands(), 10U);
        EXPECT_FLOAT_EQ(processing.equalizer.preamp(), -3.0F);
        EXPECT_FLOAT_EQ(processing.volume.gain(), 0.25F);
        const auto outcomes{session.take_outcomes()};
        const auto completion{std::ranges::find_if(outcomes.values, [&](const auto& value) {
            return value.operation == wiola::engine::OperationKind::open && value.id == request;
        })};
        ASSERT_NE(completion, outcomes.values.end());
        ASSERT_TRUE(completion->error);
        const auto* error{std::get_if<wiola::engine::OutputSetupError>(&*completion->error)};
        ASSERT_NE(error, nullptr);
        EXPECT_FALSE(error->detail.empty());
        if (fault == Fault::exception) {
            EXPECT_EQ(error->detail, "controlled device failure");
        }
        fail = false;
        EXPECT_EQ(load_and_wait(session, "candidate"), OpenResult::opened);
        EXPECT_EQ(session.track(), std::filesystem::path{"candidate"});
        EXPECT_EQ(processing.equalizer.num_bands(), 9U);
    }
}

TEST(Session, NullDecoderCompletionReportsFailureWithoutOpeningOutput)
{
    std::size_t outputs{0};
    wiola::audio::ProcessingChain processing;
    Session session{processing,
        [&](wiola::audio::Source&) {
            ++outputs;
            return std::make_unique<wiola::testing::FakeOutput>();
        },
        std::make_shared<wiola::testing::FakeMediaProvider>(
            [](const std::filesystem::path&, const wiola::media::OperationContext&) {
                return wiola::media::DecoderOpenResult{
                    std::unique_ptr<wiola::media::AudioDecoder>{}};
            },
            [](const std::filesystem::path&) { return wiola::media::Metadata{}; }
        )};
    EXPECT_EQ(load_and_wait(session, "empty-decoder"), OpenResult::damaged);
    EXPECT_FALSE(session.loaded());
    EXPECT_EQ(outputs, 0U);
    const auto outcomes{session.take_outcomes()};
    const auto opened{std::ranges::find_if(outcomes.values,
        [](const auto& value) { return value.operation == wiola::engine::OperationKind::open; })};
    ASSERT_NE(opened, outcomes.values.end());
    ASSERT_TRUE(opened->error);
    EXPECT_EQ(std::get<wiola::media::Error>(*opened->error).category,
        wiola::media::ErrorCategory::damaged);
}

TEST(Session, InvalidStreamCompletionCannotReportASuccessfulOpen)
{
    wiola::audio::ProcessingChain processing;
    Session session{processing, wiola::testing::passive_output,
        std::make_shared<wiola::testing::FakeMediaProvider>(
            [](const std::filesystem::path&, const wiola::media::OperationContext&) {
                return wiola::media::DecoderOpenResult{
                    std::make_unique<wiola::testing::FakeDecoder>(Frames{64},
                        wiola::pcm::StreamSpec{.sample_rate = units::Frequency{},
                            .num_channels = 2})};
            },
            [](const std::filesystem::path&) { return wiola::media::Metadata{}; }
        )};
    ASSERT_EQ(load_and_wait(session, "invalid-stream"), OpenResult::damaged);
    const auto outcomes{session.take_outcomes()};
    std::size_t open_completions{0};
    for (const auto& completion : outcomes.values) {
        if (completion.operation != wiola::engine::OperationKind::open)
            continue;
        ++open_completions;
        ASSERT_TRUE(completion.error);
        EXPECT_EQ(std::get<wiola::media::Error>(*completion.error).category,
            wiola::media::ErrorCategory::damaged);
    }
    EXPECT_EQ(open_completions, 1U);
}

TEST(Session, InvalidDecoderCapabilitiesCannotRetireTheActivePipeline)
{
    SuppliedProcessing processing;
    std::size_t outputs{0};
    Session session{processing,
        [&](wiola::audio::Source&) {
            ++outputs;
            return std::make_unique<wiola::testing::FakeOutput>();
        },
        std::make_shared<wiola::testing::FakeMediaProvider>(
            [](const std::filesystem::path& path, const wiola::media::OperationContext&) {
                if (path == "invalid.media")
                    return wiola::media::DecoderOpenResult{
                        std::make_unique<wiola::testing::FakeDecoder>(
                            wiola::media::DecoderStreamInfo{
                                                            .spec = {.sample_rate = units::Frequency{48000.0},
                                    .num_channels = 1},
                                                            .length = std::nullopt,
                                                            .length_accuracy = wiola::media::LengthAccuracy::exact}
                        )
                    };
                return wiola::media::DecoderOpenResult{
                    std::make_unique<wiola::testing::FakeDecoder>()};
            },
            [](const std::filesystem::path&) { return wiola::media::Metadata{}; }
        )};
    ASSERT_EQ(load_and_wait(session, "active.media"), OpenResult::opened);
    ASSERT_TRUE(session.play());
    const auto prepared{processing.prepared_for};
    static_cast<void>(session.take_outcomes());

    EXPECT_EQ(load_and_wait(session, "invalid.media"), OpenResult::damaged);

    EXPECT_EQ(outputs, 1U);
    EXPECT_EQ(processing.prepared_for, prepared);
    EXPECT_TRUE(session.loaded());
    EXPECT_TRUE(session.playing());
    EXPECT_EQ(session.track(), "active.media");
    const auto outcomes{session.take_outcomes()};
    const auto opened{std::ranges::find_if(outcomes.values,
        [](const auto& value) { return value.operation == wiola::engine::OperationKind::open; })};
    ASSERT_NE(opened, outcomes.values.end());
    ASSERT_TRUE(opened->error);
    const auto* error{std::get_if<wiola::media::Error>(&*opened->error)};
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(error->category, wiola::media::ErrorCategory::damaged);
    EXPECT_EQ(error->detail, "exact decoder length is missing");
}

TEST(Session, UnrepresentableBufferPoliciesFailBeforePreparingResources)
{
    const wiola::pcm::StreamSpec spec;
    const std::array durations{units::Time{std::numeric_limits<double>::min()},
        units::Time{std::numeric_limits<double>::max()},
        units::Time{static_cast<double>(std::numeric_limits<std::size_t>::max()) /
            spec.sample_rate.hz() / static_cast<double>(spec.num_channels)}};
    for (const auto duration : durations) {
        SuppliedProcessing processing;
        std::size_t outputs{0};
        Session session{processing,
            [&](wiola::audio::Source&) {
                ++outputs;
                return std::make_unique<wiola::testing::FakeOutput>();
            },
            std::make_shared<wiola::testing::FakeMediaProvider>(
                [](const std::filesystem::path&, const wiola::media::OperationContext&) {
                    return wiola::media::DecoderOpenResult{
                        std::make_unique<wiola::testing::FakeDecoder>()};
                },
                [](const std::filesystem::path&) { return wiola::media::Metadata{}; }
            ),
            {}, {.duration = duration}};

        EXPECT_EQ(load_and_wait(session, "controlled.media"), OpenResult::damaged);
        EXPECT_FALSE(session.loaded());
        EXPECT_FALSE(processing.prepared_for);
        EXPECT_EQ(outputs, 0U);
        const auto outcomes{session.take_outcomes()};
        const auto opened{std::ranges::find_if(outcomes.values, [](const auto& value) {
            return value.operation == wiola::engine::OperationKind::open;
        })};
        ASSERT_NE(opened, outcomes.values.end());
        ASSERT_TRUE(opened->error);
        const auto* error{std::get_if<wiola::media::Error>(&*opened->error)};
        ASSERT_NE(error, nullptr);
        EXPECT_EQ(error->category, wiola::media::ErrorCategory::damaged);
        EXPECT_EQ(error->detail, "decoder stream cannot support the configured playback buffer");
    }
}

TEST(Session, DeviceStartFailureIsAnEventualTransportFailureAfterInstallation)
{
    wiola::audio::ProcessingChain processing;
    Session session{processing,
        [](wiola::audio::Source&) {
            auto output{std::make_unique<wiola::testing::FakeOutput>()};
            output->refuse_starts();
            return output;
        },
        std::make_shared<wiola::testing::FakeMediaProvider>(
            [](const std::filesystem::path&, const wiola::media::OperationContext&) {
                return wiola::media::DecoderOpenResult{
                    std::make_unique<wiola::testing::FakeDecoder>()};
            },
            [](const std::filesystem::path&) { return wiola::media::Metadata{}; }
        )};
    ASSERT_EQ(load_and_wait(session, "installed"), OpenResult::opened);
    ASSERT_TRUE(session.play());
    ASSERT_TRUE(poll_until(session, [&] { return session.state() == State::faulted; }));
    EXPECT_TRUE(session.loaded());
    EXPECT_EQ(session.track(), std::filesystem::path{"installed"});
    EXPECT_EQ(session.open_result(), OpenResult::opened);
    const auto outcomes{session.take_outcomes()};
    std::size_t opened{0};
    for (const auto& completion : outcomes.values) {
        if (completion.operation == wiola::engine::OperationKind::open) {
            ++opened;
            EXPECT_FALSE(completion.error);
        }
    }
    EXPECT_EQ(opened, 1U);
}

#if WIOLA_TEST_ALLOCATOR_REPLACEMENT
TEST(Session, EveryInstallationAllocationFailurePreservesActiveUnseekablePlayback)
{
    constexpr wiola::pcm::StreamSpec current_spec{.sample_rate = units::Frequency{48000.0},
        .num_channels = 2};
    constexpr wiola::pcm::StreamSpec next_spec{.sample_rate = units::Frequency{22050.0},
        .num_channels = 8};
    bool observed_failure{false};
    for (std::size_t after{0};; ++after) {
        wiola::testing::Gate opening;
        wiola::testing::FakeOutput* active_device{};
        wiola::testing::Shaping processing{wiola::pcm::StreamSpec{}};
        Session session{processing.chain,
            [&](wiola::audio::Source& rendered) {
                auto output{std::make_unique<wiola::testing::FakeOutput>()};
                if (rendered.spec() == current_spec)
                    active_device = output.get();
                return output;
            },
            std::make_shared<wiola::testing::FakeMediaProvider>(
                [&](const std::filesystem::path& path,
                    const wiola::media::OperationContext&) -> wiola::media::DecoderOpenResult {
                    if (path == "current")
                        return wiola::media::DecoderOpenResult{
                            std::make_unique<wiola::testing::FakeDecoder>(
                                wiola::media::DecoderStreamInfo{.spec = current_spec,
                                    .length = std::nullopt,
                                    .seekable = false}
                            )};
                    opening.wait();
                    return wiola::media::DecoderOpenResult{
                        std::make_unique<wiola::testing::FakeDecoder>(Frames{64}, next_spec)};
                },
                [](const std::filesystem::path&) {
                    wiola::media::Metadata tags;
                    tags.track.title = "settled";
                    return tags;
                }
            )};
        ASSERT_EQ(load_and_wait(session, "current"), OpenResult::opened);
        ASSERT_TRUE(poll_until(session,
            [&] { return session.playlist().tracks().front().metadata().title == "settled"; }));
        processing.equalizer.set_band_gain(9, 3.0F);
        processing.volume.set_gain(0.25F);
        ASSERT_TRUE(session.play());
        ASSERT_TRUE(poll_until(session, [&] { return active_device->running(); }));
        active_device->play(Frames{32});
        const auto position{session.time_played()};
        ASSERT_EQ(session.open({"candidate"}), OpenResult::loading);
        opening.wait_until_entered();
        // Settle unrelated metadata before injecting failures into the installation turn.
        const bool metadata_settled{poll_until(session,
            [&] { return session.playlist().tracks().front().metadata().title == "settled"; })};
        opening.release();
        ASSERT_TRUE(metadata_settled);
        static_cast<void>(session.take_outcomes());
        while (session.reading())
            std::this_thread::yield();
        const auto request{session.open_request_id()};
        bool failed{false};
        {
            const wiola::testing::AllocationFailure failure{after};
            try {
                session.catch_up();
            } catch (const std::bad_alloc&) {
                failed = true;
            }
        }
        const auto outcomes{session.take_outcomes()};
        const auto opened{std::ranges::find_if(outcomes.values, [&](const auto& value) {
            return value.operation == wiola::engine::OperationKind::open && value.id == request;
        })};
        if (!failed) {
            EXPECT_TRUE(observed_failure);
            EXPECT_EQ(session.track(), std::filesystem::path{"candidate"});
            EXPECT_EQ(session.open_result(), OpenResult::opened);
            ASSERT_NE(opened, outcomes.values.end());
            EXPECT_FALSE(opened->error);
            break;
        }
        observed_failure = true;
        EXPECT_EQ(opened, outcomes.values.end());
        ASSERT_TRUE(session.loaded());
        EXPECT_EQ(session.track(), std::filesystem::path{"current"});
        EXPECT_EQ(session.state(), State::playing);
        EXPECT_TRUE(active_device->running());
        EXPECT_FALSE(session.seekable());
        EXPECT_EQ(session.time_played(), position);
        EXPECT_EQ(processing.equalizer.num_bands(), 10U);
        EXPECT_FLOAT_EQ(processing.equalizer.preamp(), -3.0F);
        EXPECT_FLOAT_EQ(processing.equalizer.band_gain(9), 3.0F);
        EXPECT_FLOAT_EQ(processing.volume.gain(), 0.25F);
    }
}
#endif
