/**
 * @file
 * @brief Resource handoff, restoration intent and ticket lifetime contracts.
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
#include <wiola/audio/processing_chain.hpp>

#include <atomic>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <stop_token>

namespace {

using wiola::engine::OpenResult;
using wiola::engine::Session;
using wiola::testing::session::load_and_wait;
using wiola::testing::session::poll_until;
using wiola::testing::session::SessionFixture;
using State = wiola::engine::PlaybackState;
namespace units = wiola::units;

TEST(Session, ReplacementCommandsKeepTheLatestIntentAndStopResetsItsPosition)
{
    SessionFixture fixture;
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
    SessionFixture fixture;
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
        SessionFixture fixture;
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
    SessionFixture fixture;
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
    SessionFixture fixture;
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
    SessionFixture fixture;
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
    SessionFixture fixture;
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
    SessionFixture fixture;
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
    SessionFixture fixture;
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

} // namespace
