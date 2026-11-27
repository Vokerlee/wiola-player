/**
 * @file
 * @brief Reusable audio output conformance checks.
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

#pragma once

#include <wiola/audio/output_connection.hpp>

#include <gtest/gtest.h>

namespace wiola::testing {

/// The supplied test output must be available and retain playback until explicitly stopped.
inline void verify_output_contract(audio::Output& output)
{
    EXPECT_FALSE(output.running());
    {
        audio::OutputConnection first{output};
        audio::OutputConnection candidate{output};
        ASSERT_TRUE(first.try_acquire());
        EXPECT_FALSE(candidate.try_acquire());
        ASSERT_TRUE(first.control().start());
        EXPECT_TRUE(output.running());
        EXPECT_FALSE(candidate.control().start());

        first.stop();
        EXPECT_FALSE(output.running());
        ASSERT_TRUE(first.control().start());
        EXPECT_TRUE(output.running());
    }
    EXPECT_FALSE(output.running());

    audio::OutputConnection next{output};
    ASSERT_TRUE(next.try_acquire());
    EXPECT_TRUE(next.control().start());
}

} // namespace wiola::testing
