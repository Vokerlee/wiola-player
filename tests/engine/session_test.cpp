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

#include <fixtures/wav.hpp>

#include <wiola/audio/output.hpp>

#include <codec/provider.hpp>
#include <engine/session.hpp>
#include <wiola/audio/source.hpp>
#include <wiola/media/audio_decoder.hpp>
#include <wiola/media/stream_spec.hpp>

#include <fakes/decoder.hpp>
#include <fakes/output.hpp>
#include <queue/track.hpp>
#include <wiola/units.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <memory>
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

/// A session that plays nowhere real, and counts the outputs it was asked to build.
struct Fixture {
    Fixture()
        : session{[this](wiola::audio::Source& source) {
                      ++num_outputs;
                      return consuming ? std::make_unique<wiola::testing::FakeOutput>(source)
                                       : std::make_unique<wiola::testing::FakeOutput>();
                  },
              std::make_shared<wiola::codec::LocalMediaProvider>()}
    {
    }

    /// Set before loading, when a test needs a track that cannot run out from under it.
    bool consuming{true};

    std::size_t num_outputs{0};
    Session session;
};

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

    EXPECT_FALSE(fixture.session.play_or_pause());

    fixture.session.stop();
    fixture.session.seek(units::Time{1.0});

    EXPECT_FALSE(fixture.session.loaded());
    EXPECT_EQ(fixture.num_outputs, 0U);
}

TEST(Session, LoadsATrack)
{
    Fixture fixture;

    ASSERT_EQ(load_and_wait(fixture.session, wiola::testing::write_wav("wiola_session.wav")),
        OpenResult::opened);

    EXPECT_TRUE(fixture.session.loaded());
    EXPECT_EQ(fixture.num_outputs, 1U);
    ASSERT_TRUE(fixture.session.total_time());
    EXPECT_NEAR(fixture.session.total_time()->get<units::Sec>(), wiola::testing::source_seconds,
        0.01);
}

TEST(Session, RejectsAnInvalidStreamBeforeBuildingAPipeline)
{
    std::size_t num_outputs{0};
    Session session{[&num_outputs](wiola::audio::Source&) {
                        ++num_outputs;
                        return std::make_unique<wiola::testing::FakeOutput>();
                    },
        [](const std::filesystem::path&, const wiola::media::OperationContext&) {
            auto decoder{std::make_unique<wiola::testing::FakeDecoder>(Frames{64},
                wiola::pcm::StreamSpec{.sample_rate = units::Frequency{48000.0},
                    .num_channels = 0})};
            return wiola::media::DecoderOpenResult{std::move(decoder)};
        },
        [](const std::filesystem::path&) { return wiola::media::Metadata{}; }};

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
        Session session{
            [](wiola::audio::Source&) { return std::make_unique<wiola::testing::FakeOutput>(); },
            [](const std::filesystem::path&, const wiola::media::OperationContext&) {
                return wiola::media::DecoderOpenResult{
                    std::make_unique<wiola::testing::FakeDecoder>()};
            },
            [](const std::filesystem::path&) { return wiola::media::Metadata{}; },
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

    const std::filesystem::path path{wiola::testing::write_wav("wiola_session.wav")};

    ASSERT_EQ(load_and_wait(fixture.session, path), OpenResult::opened);

    EXPECT_EQ(fixture.session.track(), path);
}

/// The name follows what is playing, not what was asked for: a file that would not open leaves
/// both alone.
TEST(Session, KeepsTheNameOfTheTrackAFailedLoadDidNotReplace)
{
    Fixture fixture;

    const std::filesystem::path path{wiola::testing::write_wav("wiola_session.wav")};

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

    ASSERT_EQ(load_and_wait(fixture.session, wiola::testing::write_wav("wiola_session.wav")),
        OpenResult::opened);
    ASSERT_TRUE(fixture.session.play_or_pause());
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

    ASSERT_EQ(load_and_wait(fixture.session, wiola::testing::write_wav("wiola_session.wav")),
        OpenResult::opened);
    ASSERT_TRUE(fixture.session.play_or_pause());

    EXPECT_EQ(fixture.session.open({wiola::testing::write_wav("wiola_other.wav")}),
        OpenResult::loading);
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
    const std::filesystem::path path{wiola::testing::write_wav("wiola_session.wav")};

    ASSERT_EQ(load_and_wait(fixture.session, path), OpenResult::opened);
    ASSERT_EQ(load_and_wait(fixture.session, path), OpenResult::opened);

    EXPECT_EQ(fixture.num_outputs, 1U);
}

/// A format it cannot play is a device it has to open again: what is open was cut for the rate
/// of the track before.
TEST(Session, OpensAnotherDeviceWhenTheFormatChanges)
{
    Fixture fixture;

    ASSERT_EQ(load_and_wait(fixture.session, wiola::testing::write_wav("wiola_session.wav")),
        OpenResult::opened);
    ASSERT_EQ(fixture.num_outputs, 1U);

    ASSERT_EQ(load_and_wait(fixture.session,
                  wiola::testing::write_wav("wiola_session_48k.wav", 48000)),
        OpenResult::opened);

    EXPECT_EQ(fixture.num_outputs, 2U);
}

TEST(Session, PlaysAndPauses)
{
    Fixture fixture;

    ASSERT_EQ(load_and_wait(fixture.session, wiola::testing::write_wav("wiola_session.wav")),
        OpenResult::opened);

    ASSERT_TRUE(fixture.session.play_or_pause());
    EXPECT_EQ(fixture.session.state(), State::playing);
    EXPECT_TRUE(fixture.session.playing());

    ASSERT_TRUE(fixture.session.play_or_pause());
    EXPECT_EQ(fixture.session.state(), State::paused);

    ASSERT_TRUE(fixture.session.play_or_pause());
    EXPECT_EQ(fixture.session.state(), State::playing);
}

TEST(Session, StopsAtTheBeginning)
{
    Fixture fixture;

    ASSERT_EQ(load_and_wait(fixture.session, wiola::testing::write_wav("wiola_session.wav")),
        OpenResult::opened);
    ASSERT_TRUE(fixture.session.play_or_pause());

    fixture.session.seek(units::Time{1.0});
    fixture.session.stop();

    EXPECT_EQ(fixture.session.state(), State::stopped);
    EXPECT_EQ(fixture.session.time_played(), units::Time{});
}

TEST(Session, SeeksWhereItIsAsked)
{
    Fixture fixture;

    ASSERT_EQ(load_and_wait(fixture.session, wiola::testing::write_wav("wiola_session.wav")),
        OpenResult::opened);

    fixture.session.seek(units::Time{1.0});

    EXPECT_NEAR(fixture.session.time_played().get<units::Sec>(), 1.0, 0.01);
}

/// A setting belongs to the listener, not to what is playing.
TEST(Session, KeepsItsSettingsAcrossTracks)
{
    Fixture fixture;
    const std::filesystem::path path{wiola::testing::write_wav("wiola_session.wav")};

    fixture.session.volume().set_gain(0.25F);
    fixture.session.equalizer().set_band_gain(3, 6.0F);

    ASSERT_EQ(load_and_wait(fixture.session, path), OpenResult::opened);

    EXPECT_FLOAT_EQ(fixture.session.volume().gain(), 0.25F);
    EXPECT_FLOAT_EQ(fixture.session.equalizer().band_gain(3), 6.0F);
}

} // namespace

/// The point of a list: a track running out is the cue to play the next one.
TEST(Session, PlaysTheNextTrackWhenOneEnds)
{
    Fixture fixture;

    const std::filesystem::path first{wiola::testing::write_wav("wiola_session.wav")};
    const std::filesystem::path next{wiola::testing::write_wav("wiola_session_next.wav")};

    ASSERT_EQ(fixture.session.open({first, next}), OpenResult::loading);
    ASSERT_TRUE(poll_until(fixture.session, [&fixture] { return fixture.session.loaded(); }));
    ASSERT_TRUE(fixture.session.play_or_pause());

    EXPECT_TRUE(poll_until(fixture.session, [&fixture, &next] {
        return fixture.session.track() == next;
    })) << "the next track was never played";
    EXPECT_TRUE(poll_until(fixture.session, [&fixture] { return fixture.session.playing(); }));
}

/// The end of the list is the end of playback: it stays where it stopped.
TEST(Session, StaysAtTheEndOfTheList)
{
    Fixture fixture;

    const std::filesystem::path only{wiola::testing::write_wav("wiola_session.wav")};

    ASSERT_EQ(load_and_wait(fixture.session, only), OpenResult::opened);
    ASSERT_TRUE(fixture.session.play_or_pause());

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

    const std::filesystem::path first{wiola::testing::write_wav("wiola_session.wav")};
    const std::filesystem::path next{wiola::testing::write_wav("wiola_session_next.wav")};

    ASSERT_EQ(fixture.session.open({first, next}), OpenResult::loading);
    ASSERT_TRUE(poll_until(fixture.session, [&fixture] { return fixture.session.loaded(); }));
    ASSERT_TRUE(fixture.session.play_or_pause());

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

    const std::filesystem::path first{wiola::testing::write_wav("wiola_session.wav")};
    const std::filesystem::path next{wiola::testing::write_wav("wiola_session_next.wav")};

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

    const std::filesystem::path first{wiola::testing::write_wav("wiola_session.wav")};
    const std::filesystem::path added{wiola::testing::write_wav("wiola_session_added.wav")};

    ASSERT_EQ(load_and_wait(fixture.session, first), OpenResult::opened);
    ASSERT_TRUE(fixture.session.play_or_pause());

    fixture.session.add({added});

    EXPECT_EQ(fixture.session.playlist().tracks().size(), 2U);
    EXPECT_EQ(fixture.session.track(), first);
    EXPECT_EQ(fixture.session.state(), State::playing);
}

/// What is added is read too, and every one of them.
TEST(Session, TakesUpWhatEveryAddedTrackSays)
{
    Fixture fixture;

    const std::vector<std::filesystem::path> both{std::filesystem::path{WIOLA_TEST_DATA_DIR} /
            "tone.flac",
        std::filesystem::path{WIOLA_TEST_DATA_DIR} / "tone.wav"};

    fixture.session.add(both);

    EXPECT_TRUE(poll_until(fixture.session, [&fixture] {
        const std::vector<wiola::engine::Track>& queued{fixture.session.playlist().tracks()};

        return queued.size() == 2 && queued.front().duration().has_value() &&
            queued.back().duration().has_value();
    })) << "a track that was added was never read";
}

/// Adding to an empty queue is the first thing a listener does, and they mean to play it.
TEST(Session, ReadsTheFirstTrackAddedToAnEmptyQueue)
{
    Fixture fixture;

    const std::filesystem::path added{wiola::testing::write_wav("wiola_session.wav")};

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

    const std::filesystem::path first{wiola::testing::write_wav("wiola_session.wav")};
    const std::filesystem::path next{wiola::testing::write_wav("wiola_session_next.wav")};

    ASSERT_EQ(fixture.session.open({first, next}), OpenResult::loading);
    ASSERT_TRUE(poll_until(fixture.session, [&fixture] { return fixture.session.loaded(); }));
    ASSERT_TRUE(fixture.session.play_or_pause());

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

    ASSERT_EQ(fixture.session.open({wiola::testing::write_wav("wiola_session.wav"),
                  wiola::testing::write_wav("wiola_session_next.wav")}),
        OpenResult::loading);
    ASSERT_TRUE(poll_until(fixture.session, [&fixture] { return fixture.session.loaded(); }));
    ASSERT_TRUE(fixture.session.play_or_pause());

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

    const std::filesystem::path track{std::filesystem::path{WIOLA_TEST_DATA_DIR} / "tone.flac"};

    ASSERT_EQ(fixture.session.open({track}), OpenResult::loading);

    ASSERT_TRUE(poll_until(fixture.session, [&fixture] {
        return fixture.session.playlist().tracks().front().duration().has_value();
    })) << "how long the track runs was never found out";

    const wiola::engine::Track& described{fixture.session.playlist().tracks().front()};

    ASSERT_TRUE(described.duration().has_value());
    EXPECT_GT(*described.duration(), units::Time{});
    EXPECT_FALSE(described.title().empty());
}

/// Every track of a list is read, not only the first: opening several is the one way a listener
/// fills a queue in one go.
TEST(Session, TakesUpWhatEveryTrackOfAnOpenedListSays)
{
    Fixture fixture;

    const std::filesystem::path first{std::filesystem::path{WIOLA_TEST_DATA_DIR} / "tone.flac"};
    const std::filesystem::path next{std::filesystem::path{WIOLA_TEST_DATA_DIR} / "tone.wav"};
    const std::vector<std::filesystem::path> both{first, next};

    ASSERT_EQ(fixture.session.open(both), OpenResult::loading);

    ASSERT_TRUE(poll_until(fixture.session, [&fixture] {
        const std::vector<wiola::engine::Track>& queued{fixture.session.playlist().tracks()};

        return queued.size() == 2 && queued.front().duration().has_value() &&
            queued.back().duration().has_value();
    })) << "a track of the list was never read";
}

/// Until then it is a track with the file's own name, rather than a row waiting for something.
TEST(Session, NamesAQueuedTrackAfterItsFileToBeginWith)
{
    Fixture fixture;

    const std::filesystem::path track{wiola::testing::write_wav("wiola_session.wav")};

    ASSERT_EQ(fixture.session.open({track}), OpenResult::loading);

    EXPECT_EQ(fixture.session.playlist().tracks().front().title(), track.stem().string());
}

/// What a listener picking a track out of their queue asks for.
TEST(Session, PlaysTheTrackItIsSentTo)
{
    Fixture fixture;
    fixture.consuming = false;

    const std::filesystem::path first{wiola::testing::write_wav("wiola_session.wav")};
    const std::filesystem::path next{wiola::testing::write_wav("wiola_session_next.wav")};

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

    const std::filesystem::path first{wiola::testing::write_wav("wiola_session.wav")};
    const std::filesystem::path next{wiola::testing::write_wav("wiola_session_next.wav")};

    ASSERT_EQ(fixture.session.open({first, next}), OpenResult::loading);
    fixture.session.shuffle(true);

    ASSERT_EQ(fixture.session.playlist().tracks().size(), 2U);
    EXPECT_EQ(fixture.session.playlist().tracks().front().path(), first);
    EXPECT_EQ(fixture.session.playlist().tracks().back().path(), next);
}

TEST(Session, GoesNowhereBeyondTheEndsOfTheList)
{
    Fixture fixture;

    ASSERT_EQ(load_and_wait(fixture.session, wiola::testing::write_wav("wiola_session.wav")),
        OpenResult::opened);

    EXPECT_FALSE(fixture.session.next_track());
    EXPECT_FALSE(fixture.session.previous_track());
}

/// Skipping while a track plays plays the one skipped to; skipping while nothing does, does not.
TEST(Session, KeepsPlayingOrNotWhenTheTrackChanges)
{
    Fixture fixture;
    fixture.consuming = false;

    const std::filesystem::path first{wiola::testing::write_wav("wiola_session.wav")};
    const std::filesystem::path next{wiola::testing::write_wav("wiola_session_next.wav")};

    ASSERT_EQ(fixture.session.open({first, next}), OpenResult::loading);
    ASSERT_TRUE(poll_until(fixture.session, [&fixture] { return fixture.session.loaded(); }));
    ASSERT_TRUE(fixture.session.play_or_pause());

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
    Session session{
        [](wiola::audio::Source&) { return std::make_unique<wiola::testing::FakeOutput>(); },
        [](const std::filesystem::path&, const wiola::media::OperationContext&) {
            return wiola::media::DecoderOpenResult{std::make_unique<wiola::testing::FakeDecoder>()};
        },
        [&num_reads](const std::filesystem::path&) {
            ++num_reads;
            wiola::media::Metadata metadata;
            metadata.track.title = "read";
            return metadata;
        }};
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
    Session session{
        [](wiola::audio::Source&) { return std::make_unique<wiola::testing::FakeOutput>(); },
        [](const std::filesystem::path&, const wiola::media::OperationContext&) {
            return wiola::media::DecoderOpenResult{std::make_unique<wiola::testing::FakeDecoder>()};
        },
        [](const std::filesystem::path&) -> wiola::media::Result<wiola::media::Metadata> {
            return wiola::media::unexpected{
                wiola::media::Error{.category = wiola::media::ErrorCategory::unsupported,
                                    .detail = "no tag format"}
            };
        }};

    EXPECT_EQ(session.open({"playable.wav"}), OpenResult::loading);
    ASSERT_TRUE(poll_until(session,
        [&session] { return session.loaded() && session.playlist().metadata_revision() == 1U; }));

    EXPECT_EQ(session.open_result(), OpenResult::opened);
    EXPECT_TRUE(session.loaded());
    EXPECT_EQ(session.playlist().tracks().front().title(), "playable");
}

TEST(Session, ClearDuringLoadPreventsAnObsoleteInstall)
{
    wiola::testing::Gate opening;
    Session session{
        [](wiola::audio::Source&) { return std::make_unique<wiola::testing::FakeOutput>(); },
        [&opening](const std::filesystem::path&, const wiola::media::OperationContext&) {
            opening.wait();
            return wiola::media::DecoderOpenResult{std::make_unique<wiola::testing::FakeDecoder>()};
        },
        [](const std::filesystem::path&) { return wiola::media::Metadata{}; }};

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
    Session session{
        [](wiola::audio::Source&) { return std::make_unique<wiola::testing::FakeOutput>(); },
        [&opening, &second](const std::filesystem::path& path,
            const wiola::media::OperationContext&) {
            if (path == second)
                opening.wait();
            return wiola::media::DecoderOpenResult{std::make_unique<wiola::testing::FakeDecoder>()};
        },
        [](const std::filesystem::path&) { return wiola::media::Metadata{}; }};

    session.open({"first.wav", second});
    ASSERT_TRUE(poll_until(session, [&session] { return session.loaded(); }));
    ASSERT_TRUE(session.play_or_pause());
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
    Session session{wiola::testing::passive_output,
        [&](const std::filesystem::path&, const wiola::media::OperationContext& context) {
            std::stop_callback callback{context.stop, [&] { cancelled = true; }};
            opening.wait();
            return wiola::media::DecoderOpenResult{std::make_unique<wiola::testing::FakeDecoder>()};
        },
        [](const std::filesystem::path&) { return wiola::media::Metadata{}; }};
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
    Session session{wiola::testing::passive_output,
        [&](const std::filesystem::path&, const wiola::media::OperationContext& context) {
            std::stop_callback callback{context.stop, [&] { cancelled = true; }};
            opening.wait();
            return wiola::media::DecoderOpenResult{std::make_unique<wiola::testing::FakeDecoder>()};
        },
        [](const std::filesystem::path&) { return wiola::media::Metadata{}; }};
    session.open({"late.wav"});
    opening.wait_until_entered();
    session.open({});
    EXPECT_TRUE(cancelled);
    opening.release();
    ASSERT_TRUE(poll_until(session, [&] { return !session.reading(); }));
    EXPECT_FALSE(session.loaded());
    EXPECT_TRUE(session.playlist().empty());
}

TEST(Session, ReloadQuiescesAndRestoresTheCurrentPlayIntent)
{
    Fixture fixture;
    fixture.consuming = false;
    const auto path = wiola::testing::write_wav("wiola_session_reload.wav");

    ASSERT_EQ(load_and_wait(fixture.session, path), OpenResult::opened);
    fixture.session.add({wiola::testing::write_wav("wiola_session_reload_next.wav")});
    ASSERT_TRUE(fixture.session.play_or_pause());

    fixture.session.seek(units::Time{0.4});
    ASSERT_TRUE(poll_until(fixture.session,
        [&fixture] { return fixture.session.time_played().get<units::Sec>() >= 0.4; }));

    EXPECT_EQ(fixture.session.reload_current(), OpenResult::loading);
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
    const auto path = wiola::testing::write_wav("wiola_session_handoff.wav");
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
    const auto path = wiola::testing::write_wav("wiola-ticket-generation.wav");
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
