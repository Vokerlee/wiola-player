/**
 * @file
 * @brief Unit tests for what is to be played and where in it playback has reached.
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

#include <gtest/gtest.h>
#include <queue/playback_order.hpp>

namespace {

using wiola::engine::PlaybackOrder;

TEST(PlaybackOrder, EmptyShuffleAndUnshufflePreserveTheUnselectedState)
{
    PlaybackOrder order;
    order.reset(0);
    order.shuffle(0);
    EXPECT_TRUE(order.shuffled());
    EXPECT_FALSE(order.position());
    order.unshuffle();
    EXPECT_FALSE(order.shuffled());
    EXPECT_FALSE(order.position());
    order.add();
    EXPECT_EQ(order.position(), 0U);
}

TEST(PlaybackOrder, OutOfRangeRemovalPreservesTheSelectedOccurrence)
{
    PlaybackOrder order;
    EXPECT_FALSE(order.remove(0));
    order.reset(3);
    ASSERT_TRUE(order.go_to(1));
    EXPECT_FALSE(order.remove(3));
    EXPECT_EQ(order.position(), 1U);
}

} // namespace
