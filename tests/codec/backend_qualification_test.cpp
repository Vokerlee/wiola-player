/**
 * @file
 * @brief Guards the currently qualified playback set during future backend evaluation.
 * @author Roman Glaz
 * @copyright © 2026, <vokerlee@gmail.com>
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

#include <codec/decode/registry.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

TEST(BackendQualification, AdvertisesOnlyTheCurrentlyVerifiedPlaybackSet)
{
    const wiola::codec::MediaBackendRegistry registry;
    std::vector<std::string> extensions{registry.playback_extensions()};
    std::ranges::sort(extensions);

    EXPECT_EQ(extensions,
        (std::vector<std::string>{".flac", ".mp2", ".mp3", ".mpga", ".wav", ".wave"}));

    ASSERT_EQ(registry.backends().size(), 3U);
    for (const auto& backend : registry.backends()) {
        EXPECT_TRUE(backend.capabilities.playback);
        EXPECT_TRUE(backend.capabilities.metadata);
        EXPECT_TRUE(backend.open);
    }
}
