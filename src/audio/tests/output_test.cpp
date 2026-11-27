/**
 * @file
 * @brief Unit tests for the end of an output that starts and stops it.
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

#include "output_contract.hpp"

#include <fakes/output.hpp>
#include <wiola/audio/output.hpp>
#include <wiola/audio/output_connection.hpp>
#include <wiola/audio/source.hpp>

#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <optional>
#include <semaphore>
#include <thread>
#include <type_traits>
#include <utility>

namespace {

using wiola::audio::Output;
using wiola::testing::FakeOutput;

class BlockingSource final : public wiola::audio::Source {
public:
    std::size_t render(std::span<float>) noexcept override
    {
        entered.release();
        release.acquire();

        return 0;
    }

    [[nodiscard]] wiola::pcm::StreamSpec spec() const noexcept override { return {}; }

    std::binary_semaphore entered{0};
    std::binary_semaphore release{0};
};

} // namespace

TEST(OutputControl, StartsAndStopsWhatItWasTakenFrom)
{
    wiola::core::Lease<FakeOutput> output = wiola::core::Lease<FakeOutput>::make();
    Output::Control control{wiola::audio::Output::acquire(output)};

    EXPECT_TRUE(control.start());
    EXPECT_TRUE(output->running());
    EXPECT_EQ(output->num_starts(), 1);

    control.stop();

    EXPECT_FALSE(output->running());
    EXPECT_EQ(output->num_stops(), 1);
}

TEST(OutputConnection, StopWaitsForTheSourceCallbackToReturn)
{
    BlockingSource source;
    auto output = wiola::core::Lease<FakeOutput>::make(source);
    wiola::audio::OutputConnection connection{output};

    ASSERT_TRUE(connection.control().start());

    source.entered.acquire();

    std::binary_semaphore stopped{0};
    std::jthread stopping{[&] {
        connection.stop();
        stopped.release();
    }};

    EXPECT_FALSE(stopped.try_acquire()) << "stop returned while the source callback was blocked";

    source.release.release();
    stopped.acquire();

    EXPECT_EQ(output->num_stops(), 1);
}

TEST(OutputConnection, DestructionStopsOutputBeforeReturningTheClaim)
{
    wiola::core::Lease<FakeOutput> output = wiola::core::Lease<FakeOutput>::make();
    {
        wiola::audio::OutputConnection connection{output};
        ASSERT_TRUE(connection.control().start());
    }

    EXPECT_FALSE(output->running());
    EXPECT_EQ(output->num_stops(), 1);
    EXPECT_TRUE(wiola::audio::Output::acquire(output).start());
}

TEST(OutputConnection, InertConnectionDestructionDoesNotStopTheLiveOutput)
{
    wiola::core::Lease<FakeOutput> output = wiola::core::Lease<FakeOutput>::make();
    wiola::audio::OutputConnection active{output};

    ASSERT_TRUE(active.control().start());

    {
        const wiola::audio::OutputConnection inert{output};
    }

    EXPECT_TRUE(output->running());
    EXPECT_EQ(output->num_stops(), 0);
    EXPECT_FALSE(wiola::audio::Output::acquire(output).start());
}

TEST(OutputControl, SaysWhenTheOutputWouldNotStart)
{
    wiola::core::Lease<FakeOutput> output = wiola::core::Lease<FakeOutput>::make();
    output->refuse_starts();

    Output::Control control{wiola::audio::Output::acquire(output)};

    EXPECT_FALSE(control.start());
    EXPECT_FALSE(output->running());
    EXPECT_EQ(output->num_starts(), 1);
}

/// Two threads driving one device leaves it in a state neither asked for, so there is one
/// control: a second is inert, which shows as a device that will not start.
TEST(OutputControl, HandsOutOneControlOnly)
{
    wiola::core::Lease<FakeOutput> output = wiola::core::Lease<FakeOutput>::make();
    Output::Control first{wiola::audio::Output::acquire(output)};
    Output::Control second{wiola::audio::Output::acquire(output)};

    EXPECT_FALSE(second.start());
    EXPECT_EQ(output->num_starts(), 0) << "a second control reached the output";

    second.stop();
    EXPECT_EQ(output->num_stops(), 0);

    // The one that was taken first still drives it.
    EXPECT_TRUE(first.start());
    EXPECT_EQ(output->num_starts(), 1);
}

/// Moving is how the device passes to another thread, so what is left behind must not still
/// reach it.
TEST(OutputControl, LeavesNothingBehindWhenItIsMoved)
{
    wiola::core::Lease<FakeOutput> output = wiola::core::Lease<FakeOutput>::make();
    Output::Control control{wiola::audio::Output::acquire(output)};
    Output::Control moved{std::move(control)};

    EXPECT_FALSE(control.start()); // NOLINT(bugprone-use-after-move) - the point of the test
    EXPECT_EQ(output->num_starts(), 0) << "the control that was moved from reached the output";

    EXPECT_TRUE(moved.start());
    EXPECT_EQ(output->num_starts(), 1);
}

TEST(OutputControl, LeavesNothingBehindWhenItIsMoveAssigned)
{
    wiola::core::Lease<FakeOutput> output = wiola::core::Lease<FakeOutput>::make();
    wiola::core::Lease<FakeOutput> other = wiola::core::Lease<FakeOutput>::make();

    Output::Control control{wiola::audio::Output::acquire(output)};
    Output::Control target{wiola::audio::Output::acquire(other)};

    target = std::move(control);

    EXPECT_FALSE(control.start()); // NOLINT(bugprone-use-after-move) - the point of the test
    EXPECT_EQ(output->num_starts(), 0);

    EXPECT_TRUE(target.start());
    EXPECT_EQ(output->num_starts(), 1);
    EXPECT_EQ(other->num_starts(), 0);
}

TEST(OutputControl, SelfMovePreservesTheExclusiveClaim)
{
    wiola::core::Lease<FakeOutput> output = wiola::core::Lease<FakeOutput>::make();
    Output::Control control{wiola::audio::Output::acquire(output)};
    auto& same{control};

    control = std::move(same);

    EXPECT_TRUE(control.start());
    EXPECT_FALSE(wiola::audio::Output::acquire(output).start());
    EXPECT_EQ(output->num_starts(), 1);
}

TEST(Source, DestructionThroughThePortReleasesDerivedResources)
{
    class OwnedSource final : public wiola::audio::Source {
    public:
        explicit OwnedSource(bool& destroyed)
            : destroyed_{destroyed}
        {
        }

        ~OwnedSource() override { destroyed_ = true; }

        std::size_t render(std::span<float>) noexcept override { return 0; }

        wiola::pcm::StreamSpec spec() const noexcept override { return {}; }

    private:
        bool& destroyed_;
    };

    bool destroyed{false};
    std::unique_ptr<wiola::audio::Source> source{std::make_unique<OwnedSource>(destroyed)};

    EXPECT_FALSE(destroyed);

    source.reset();

    EXPECT_TRUE(destroyed);
}

/// A device outlives the tracks played through it, so what drove it for one track gives it back
/// for the next.
TEST(OutputControl, HandsOutAnotherOnceTheHeldOneIsGone)
{
    wiola::core::Lease<FakeOutput> output = wiola::core::Lease<FakeOutput>::make();

    {
        Output::Control first{wiola::audio::Output::acquire(output)};

        ASSERT_TRUE(first.start());
        ASSERT_EQ(output->num_starts(), 1);
    }

    Output::Control next{wiola::audio::Output::acquire(output)};

    EXPECT_TRUE(next.start()) << "the device was not given back";
    EXPECT_EQ(output->num_starts(), 2);
}

/// Moving one about does not give the device back: it is still held, just elsewhere.
TEST(OutputControl, KeepsTheDeviceWhileTheControlIsMovedAbout)
{
    wiola::core::Lease<FakeOutput> output = wiola::core::Lease<FakeOutput>::make();

    Output::Control control{wiola::audio::Output::acquire(output)};
    Output::Control moved{std::move(control)};

    Output::Control second{wiola::audio::Output::acquire(output)};

    EXPECT_FALSE(second.start()) << "moving a control gave the device away";
    EXPECT_EQ(output->num_starts(), 0);

    EXPECT_TRUE(moved.start());
}

/// The one that was moved from holds nothing, so its end is not the device coming free.
TEST(OutputControl, GivesTheDeviceBackWhenTheOneHoldingItEnds)
{
    wiola::core::Lease<FakeOutput> output = wiola::core::Lease<FakeOutput>::make();
    Output::Control moved_from{wiola::audio::Output::acquire(output)};

    {
        const Output::Control holder{std::move(moved_from)};
    }

    Output::Control next{wiola::audio::Output::acquire(output)};

    EXPECT_TRUE(next.start());
    EXPECT_EQ(output->num_starts(), 1);
}

/// Being given another device to drive gives the first one back.
TEST(OutputControl, GivesBackWhatItHeldWhenItIsGivenAnother)
{
    wiola::core::Lease<FakeOutput> first = wiola::core::Lease<FakeOutput>::make();
    wiola::core::Lease<FakeOutput> second = wiola::core::Lease<FakeOutput>::make();

    Output::Control control{wiola::audio::Output::acquire(first)};
    control = wiola::audio::Output::acquire(second);

    Output::Control back{wiola::audio::Output::acquire(first)};

    EXPECT_TRUE(back.start()) << "the first device was never given back";
    EXPECT_EQ(first->num_starts(), 1);
}

TEST(OutputConnection, RepeatedAcquisitionPreservesItsExistingClaim)
{
    wiola::core::Lease<FakeOutput> output = wiola::core::Lease<FakeOutput>::make();
    wiola::audio::OutputConnection connection{output};

    ASSERT_TRUE(connection.control().start());

    EXPECT_TRUE(connection.try_acquire());
    EXPECT_TRUE(connection.try_acquire());

    EXPECT_TRUE(output->running());
    EXPECT_EQ(output->num_starts(), 1);
    EXPECT_EQ(output->num_stops(), 0);
    EXPECT_FALSE(static_cast<bool>(wiola::audio::Output::acquire(output)));
}

TEST(OutputConnection, CandidateCanAcquireAfterThePreviousConnectionIsDestroyed)
{
    wiola::core::Lease<FakeOutput> output = wiola::core::Lease<FakeOutput>::make();
    auto previous{std::make_unique<wiola::audio::OutputConnection>(output)};

    ASSERT_TRUE(previous->control().start());

    wiola::audio::OutputConnection candidate{output};

    EXPECT_FALSE(candidate.try_acquire());
    EXPECT_TRUE(output->running());
    EXPECT_EQ(output->num_stops(), 0);

    previous.reset();

    ASSERT_TRUE(candidate.try_acquire());
    ASSERT_TRUE(candidate.control().start());
    EXPECT_TRUE(output->running());
    EXPECT_EQ(output->num_starts(), 2);
    EXPECT_EQ(output->num_stops(), 1);
}

TEST(OutputConnection, OnlyOnePreparedCandidateAcquiresTheReturnedClaim)
{
    wiola::core::Lease<FakeOutput> output = wiola::core::Lease<FakeOutput>::make();
    auto previous{std::make_unique<wiola::audio::OutputConnection>(output)};
    wiola::audio::OutputConnection first{output};
    wiola::audio::OutputConnection second{output};

    previous.reset();

    EXPECT_TRUE(first.try_acquire());
    EXPECT_FALSE(second.try_acquire());
    EXPECT_TRUE(first.control().start());
    EXPECT_FALSE(second.control().start());
    EXPECT_EQ(output->num_starts(), 1);
}

TEST(OutputConnection, SatisfiesTheReusableOutputContract)
{
    wiola::core::Lease<FakeOutput> output = wiola::core::Lease<FakeOutput>::make();
    wiola::testing::verify_output_contract(output);
    EXPECT_FALSE(output->running());
}

TEST(OutputControl, RetainsItsDeviceAfterTheOriginalOwnerIsReleased)
{
    std::optional<Output::Control> control;
    wiola::core::WeakLease<FakeOutput> observer;

    {
        auto output = wiola::core::Lease<FakeOutput>::make();
        observer = output.weak();
        control = Output::acquire(output);
    }

    ASSERT_TRUE(observer.lock());
    ASSERT_TRUE(control->start());
    control->stop();

    control.reset();
    EXPECT_FALSE(observer.lock());
}

TEST(OutputControl, ActiveCallsRetainTheDeviceThroughReentrantControlDisposal)
{
    class ReentrantOutput final : public Output {
    public:
        ReentrantOutput(std::function<void()> call, bool& destroyed)
            : call_{std::move(call)}
            , destroyed_{destroyed}
        {
        }

        ~ReentrantOutput() override { destroyed_ = true; }

        bool running() const noexcept override { return false; }

        wiola::pcm::Frames frames_played() const noexcept override { return {}; }

    private:
        bool start() noexcept override
        {
            call_();
            return true;
        }

        void stop() noexcept override { call_(); }

        std::function<void()> call_;
        bool& destroyed_;
    };

    for (const bool starting : {false, true}) {
        bool destroyed = false;
        std::optional<Output::Control> control;
        {
            const auto output = wiola::core::Lease<ReentrantOutput>::make(
                [&] {
                    control.reset();
                    EXPECT_FALSE(destroyed);
                },
                destroyed
            );
            control = Output::acquire(output);
        }

        if (starting)
            EXPECT_TRUE(control->start());
        else
            control->stop();

        EXPECT_FALSE(control);
        EXPECT_TRUE(destroyed);
    }
}

TEST(OutputConnection, RetainsItsDeviceWhileWaitingToAcquireAnExclusiveClaim)
{
    static_assert(!std::is_constructible_v<wiola::audio::OutputConnection, Output&>);

    std::optional<Output::Control> prior;
    std::unique_ptr<wiola::audio::OutputConnection> candidate;
    wiola::core::WeakLease<FakeOutput> observer;
    {
        const auto output = wiola::core::Lease<FakeOutput>::make();
        observer = output.weak();
        prior = Output::acquire(output);
        candidate = std::make_unique<wiola::audio::OutputConnection>(output);
    }

    EXPECT_FALSE(candidate->try_acquire());
    prior.reset();
    ASSERT_TRUE(observer.lock());
    ASSERT_TRUE(candidate->try_acquire());
    ASSERT_TRUE(candidate->control().start());

    candidate.reset();
    EXPECT_FALSE(observer.lock());
}
