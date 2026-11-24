/**
 * @file
 * @brief Portable playback contract behavior.
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

#include <wiola/playback/open_result.hpp>
#include <wiola/playback/queue_entry_id.hpp>

#include <gtest/gtest.h>

#include <array>
#include <utility>

TEST(PlaybackContracts, OpenStatusPreservesPortableFailureCategories)
{
    using wiola::engine::OpenResult;
    using wiola::media::ErrorCategory;
    const std::array examples{
        std::pair{ErrorCategory::unsupported,    OpenResult::unsupported},
        std::pair{ErrorCategory::damaged,        OpenResult::damaged    },
        std::pair{ErrorCategory::unreadable,     OpenResult::unreadable },
        std::pair{ErrorCategory::not_mine,       OpenResult::unreadable },
        std::pair{ErrorCategory::read_failed,    OpenResult::unreadable },
        std::pair{ErrorCategory::seek_failed,    OpenResult::unreadable },
        std::pair{ErrorCategory::invalid_seek,   OpenResult::unreadable },
        std::pair{ErrorCategory::cancelled,      OpenResult::unreadable },
        std::pair{ErrorCategory::timed_out,      OpenResult::unreadable },
        std::pair{ErrorCategory::limit_exceeded, OpenResult::unreadable },
    };
    for (const auto& [category, expected] : examples)
        EXPECT_EQ(wiola::engine::open_failed(category), expected);
}

TEST(PlaybackContracts, QueueIdentityReservesZeroAndOrdersDistinctOccurrences)
{
    using wiola::engine::QueueEntryId;
    const std::array ids{QueueEntryId{}, QueueEntryId{1}, QueueEntryId{2}};
    EXPECT_FALSE(ids[0].valid());
    EXPECT_TRUE(ids[1].valid());
    EXPECT_TRUE(ids[2].valid());
    EXPECT_EQ(ids[1], ids[1]);
    EXPECT_NE(ids[1], ids[2]);
    EXPECT_LT(ids[1], ids[2]);
    EXPECT_GT(ids[2], ids[1]);
}
