/**
 * @file
 * @brief A validated, recoverable seek and re-prime operation.
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

#include <transport/seek_plan.hpp>

#include <gtest/gtest.h>

namespace {

using wiola::engine::SeekMailbox;
using wiola::engine::SeekOutcome;
using wiola::engine::SeekPlan;
using wiola::pcm::Frames;

TEST(SeekPlan, StartupDecisionsPreserveClaimsAndPreferExplicitTargetsToReplay)
{
    for (const bool terminal : {false, true}) {
        for (const bool rewind : {false, true}) {
            for (const bool outstanding : {false, true}) {
                SCOPED_TRACE(::testing::Message{} << terminal << rewind << outstanding);
                const SeekMailbox::Claim claim{42, Frames{9}, outstanding};
                const auto plan = SeekPlan::begin(claim, rewind, terminal);

                EXPECT_EQ(plan.claim().requested, 42U);
                EXPECT_EQ(plan.claim().target, Frames{9});
                EXPECT_EQ(plan.claim().outstanding, outstanding);
                EXPECT_EQ(plan.needs_quiescence(), !terminal);
                EXPECT_EQ(plan.action(),
                    terminal ? SeekPlan::Action::cancelled : SeekPlan::Action::prime);

                if (terminal || (!rewind && !outstanding))
                    EXPECT_FALSE(plan.target());
                else
                    EXPECT_EQ(plan.target(), outstanding ? Frames{9} : Frames{});
            }
        }
    }
}

TEST(SeekPlan, NoPendingRequestIsANoOpEvenAfterATerminalCommand)
{
    for (const bool terminal : {false, true}) {
        const auto idle = SeekPlan::pending({17, Frames{6}, false}, terminal);
        EXPECT_EQ(idle.action(), SeekPlan::Action::none);
        EXPECT_FALSE(idle.target());
        EXPECT_FALSE(idle.needs_quiescence());

        const auto pending = SeekPlan::pending({18, Frames{}, true}, terminal);
        EXPECT_EQ(pending.action(),
            terminal ? SeekPlan::Action::cancelled : SeekPlan::Action::prime);
        EXPECT_EQ(pending.needs_quiescence(), !terminal);
    }
}

TEST(SeekPlan, StopOutranksANewerRequestAndAPlanIsAnIndependentSnapshot)
{
    EXPECT_FALSE(SeekPlan::interruption(false, false));
    EXPECT_EQ(SeekPlan::interruption(false, true), SeekOutcome::superseded);
    EXPECT_EQ(SeekPlan::interruption(true, false), SeekOutcome::cancelled);
    EXPECT_EQ(SeekPlan::interruption(true, true), SeekOutcome::cancelled);

    SeekMailbox::Claim claim{1, Frames{4}, true};
    const auto plan = SeekPlan::pending(claim, false);
    claim = {2, Frames{8}, true};

    EXPECT_EQ(plan.claim().requested, 1U);
    EXPECT_EQ(plan.target(), Frames{4});
}

} // namespace
