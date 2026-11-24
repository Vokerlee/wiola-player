/**
 * @file
 * @brief Session commands, queue coordination and asynchronous loading contracts.
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

#include "fixture.hpp"

#include <fakes/decoder.hpp>
#include <fakes/output.hpp>
#include <fakes/provider.hpp>
#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using namespace wiola::units::literals;
using wiola::engine::OpenResult;
using wiola::engine::Session;
using wiola::pcm::Frames;
using wiola::testing::session::load_and_wait;
using wiola::testing::session::poll_until;
using wiola::testing::session::SessionFixture;
using State = wiola::engine::PlaybackState;
namespace units = wiola::units;

static_assert(!noexcept(std::declval<const Session&>().snapshot()));
static_assert(!std::is_default_constructible_v<Session>);
static_assert(!std::is_constructible_v<Session, wiola::engine::OutputFactory>);

TEST(Session, StartsWithNothingLoaded)
{
    const SessionFixture fixture;

    EXPECT_FALSE(fixture.session.loaded());
    EXPECT_EQ(fixture.session.state(), State::idle);
    EXPECT_FALSE(fixture.session.playing());
    EXPECT_EQ(fixture.session.total_time(), std::nullopt);
    EXPECT_EQ(fixture.session.time_played(), units::Time{});
}

/// Nothing loaded is not a reason to refuse to be asked.
TEST(Session, AnswersWhileNothingIsLoaded)
{
    SessionFixture fixture;

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
    SessionFixture fixture;

    ASSERT_EQ(load_and_wait(fixture.session, fixture.track()), OpenResult::opened);

    EXPECT_TRUE(fixture.session.loaded());
    EXPECT_EQ(fixture.num_outputs, 1U);
    ASSERT_TRUE(fixture.session.total_time());
    EXPECT_EQ(*fixture.session.total_time(), SessionFixture::duration);
}

TEST(Session, RefusesAFileItCannotRead)
{
    SessionFixture fixture;

    EXPECT_EQ(load_and_wait(fixture.session, std::filesystem::path{"no-such-track.wav"}),
        OpenResult::unreadable);
    EXPECT_FALSE(fixture.session.loaded());
    EXPECT_EQ(fixture.num_outputs, 0U);
}

TEST(Session, NamesNoTrackBeforeOneIsLoaded)
{
    const SessionFixture fixture;

    EXPECT_TRUE(fixture.session.track().empty());
}

TEST(Session, NamesTheTrackItLoaded)
{
    SessionFixture fixture;

    const std::filesystem::path path{fixture.track()};

    ASSERT_EQ(load_and_wait(fixture.session, path), OpenResult::opened);

    EXPECT_EQ(fixture.session.track(), path);
}

/// The name follows what is playing, not what was asked for: a file that would not open leaves
/// both alone.
TEST(Session, KeepsTheNameOfTheTrackAFailedLoadDidNotReplace)
{
    SessionFixture fixture;

    const std::filesystem::path path{fixture.track()};

    ASSERT_EQ(load_and_wait(fixture.session, path), OpenResult::opened);
    ASSERT_EQ(load_and_wait(fixture.session, std::filesystem::path{"no-such-track.wav"}),
        OpenResult::unreadable);

    EXPECT_EQ(fixture.session.track(), path);
}

TEST(Session, KeepsWhatWasLoadedWhenTheNextFileFails)
{
    SessionFixture fixture;

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
    SessionFixture fixture;

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

TEST(Session, PlaysAndPauses)
{
    SessionFixture fixture;

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
    SessionFixture fixture;

    ASSERT_EQ(load_and_wait(fixture.session, fixture.track()), OpenResult::opened);
    ASSERT_TRUE(fixture.session.play());

    fixture.session.seek(units::Time{1.0});
    fixture.session.stop();

    EXPECT_EQ(fixture.session.state(), State::stopped);
    EXPECT_EQ(fixture.session.time_played(), units::Time{});
}

TEST(Session, SeeksWhereItIsAsked)
{
    SessionFixture fixture;

    ASSERT_EQ(load_and_wait(fixture.session, fixture.track()), OpenResult::opened);

    fixture.session.seek(units::Time{1.0});

    EXPECT_NEAR(fixture.session.time_played().get<units::Sec>(), 1.0, 0.01);
}

/// The point of a list: a track running out is the cue to play the next one.
TEST(Session, PlaysTheNextTrackWhenOneEnds)
{
    SessionFixture fixture;

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
    SessionFixture fixture;

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
    SessionFixture fixture;
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
    SessionFixture fixture;
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
    SessionFixture fixture;
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
    SessionFixture fixture;

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
    SessionFixture fixture;

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
    SessionFixture fixture;
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
    SessionFixture fixture;
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
    SessionFixture fixture;

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
    SessionFixture fixture;

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
    SessionFixture fixture;

    const std::filesystem::path track{fixture.track()};

    ASSERT_EQ(fixture.session.open({track}), OpenResult::loading);

    EXPECT_EQ(fixture.session.playlist().tracks().front().metadata().title, track.stem().string());
}

/// What a listener picking a track out of their queue asks for.
TEST(Session, PlaysTheTrackItIsSentTo)
{
    SessionFixture fixture;
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
    SessionFixture fixture;

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
    SessionFixture fixture;

    ASSERT_EQ(load_and_wait(fixture.session, fixture.track()), OpenResult::opened);

    EXPECT_FALSE(fixture.session.next_track());
    EXPECT_FALSE(fixture.session.previous_track());
}

/// Skipping while a track plays plays the one skipped to; skipping while nothing does, does not.
TEST(Session, KeepsPlayingOrNotWhenTheTrackChanges)
{
    SessionFixture fixture;
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
            [&num_reads](const std::filesystem::path&, const wiola::media::OperationContext&) {
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
            [](const std::filesystem::path&, const wiola::media::OperationContext&)
                -> wiola::media::Result<wiola::media::Metadata> {
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
            [](const std::filesystem::path&, const wiola::media::OperationContext&) {
                return wiola::media::Metadata{};
            }
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
            [](const std::filesystem::path&, const wiola::media::OperationContext&) {
                return wiola::media::Metadata{};
            }
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
            [](const std::filesystem::path&, const wiola::media::OperationContext&) {
                return wiola::media::Metadata{};
            }
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
            [](const std::filesystem::path&, const wiola::media::OperationContext&) {
                return wiola::media::Metadata{};
            }
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
            [](const std::filesystem::path&, const wiola::media::OperationContext&) {
                return wiola::media::Metadata{};
            }
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

} // namespace
