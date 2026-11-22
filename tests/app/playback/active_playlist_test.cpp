/**
 * @file
 * @brief Active destination playlist regression tests.
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

#include <app/playback/active_playlist.hpp>

#include <gtest/gtest.h>

TEST(ActivePlaylist, TracksDestinationIdentityWithoutDependingOnDocumentSelection)
{
    wiola::app::ActivePlaylist active;
    const wiola::library::PlaylistId first{"playlist-1"};
    const wiola::library::PlaylistId second{"playlist-2"};

    EXPECT_FALSE(active.id().has_value());
    EXPECT_FALSE(active.set({}));
    ASSERT_TRUE(active.set(first));
    ASSERT_EQ(active.id(), first);

    EXPECT_TRUE(active.set(second));
    EXPECT_EQ(active.id(), second);
    EXPECT_FALSE(active.clear_if(first));
    EXPECT_EQ(active.id(), second);

    EXPECT_TRUE(active.clear_if(second));
    EXPECT_FALSE(active.id().has_value());
}
