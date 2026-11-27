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

#include <allocation_failure.hpp>
#include <wiola/app/library/playlist_destination.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <new>
#include <vector>

TEST(PlaylistDestination, TracksDestinationIdentityWithoutDependingOnDocumentSelection)
{
    wiola::app::PlaylistDestination active;
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

TEST(PlaylistDestination, PublishesOnlyRealChangesAndAllowsReentrantObservers)
{
    wiola::app::PlaylistDestination active;
    std::vector<std::optional<wiola::library::PlaylistId>> observed;
    active.on_changed([&](auto id) {
        observed.push_back(id);
        if (id == wiola::library::PlaylistId{"first"})
            static_cast<void>(active.set(wiola::library::PlaylistId{"second"}));
    });
    ASSERT_TRUE(active.set(wiola::library::PlaylistId{"first"}));
    EXPECT_EQ(active.id(), wiola::library::PlaylistId{"second"});
    EXPECT_EQ(observed.size(), 2);
    ASSERT_TRUE(active.set(wiola::library::PlaylistId{"second"}));
    EXPECT_EQ(observed.size(), 2);
    ASSERT_TRUE(active.clear_if(wiola::library::PlaylistId{"second"}));
    EXPECT_EQ(observed.size(), 3);
    EXPECT_FALSE(observed.back());
}

TEST(PlaylistDestination, PreservesTheMutableObserverIdentityAcrossDestinationChanges)
{
    wiola::app::PlaylistDestination active;
    std::vector<int> calls;
    active.on_changed([&, count = 0](auto) mutable { calls.push_back(++count); });
    ASSERT_TRUE(active.set({"first"}));
    ASSERT_TRUE(active.set({"second"}));
    ASSERT_TRUE(active.clear_if({"second"}));
    EXPECT_EQ(calls, (std::vector{1, 2, 3}));
}

#if WIOLA_TEST_ALLOCATOR_REPLACEMENT
TEST(PlaylistDestination, FailedNotificationPreparationCannotPublishAnUnobservedDestination)
{
    const wiola::library::PlaylistId previous{"previous"};
    const wiola::library::PlaylistId next{std::string(128, 'n')};
    bool reached_success{};
    constexpr std::size_t maximum_allocation_points = 32;

    for (std::size_t point = 0; point < maximum_allocation_points; ++point) {
        wiola::app::PlaylistDestination active;

        ASSERT_TRUE(active.set(previous));

        bool notified{};
        active.on_changed([&](auto) { notified = true; });

        bool failed{};
        {
            wiola::testing::AllocationFailure failure{point,
                wiola::testing::AllocationFailure::Mode::once};
            try {
                static_cast<void>(active.set(next));
            } catch (const std::bad_alloc&) {
                failed = true;
            }
        }

        if (failed) {
            EXPECT_EQ(active.id(), previous);
            EXPECT_FALSE(notified);
        } else {
            EXPECT_EQ(active.id(), next);
            EXPECT_TRUE(notified);

            reached_success = true;
            break;
        }
    }

    EXPECT_TRUE(reached_success);
}
#endif

TEST(PlaylistDestination, AnObserverMayReplaceItselfWithoutResettingTheActiveCall)
{
    wiola::app::PlaylistDestination destination;
    std::vector<int> calls;
    destination.on_changed([&, count = 0](auto) mutable {
        calls.push_back(++count);
        destination.on_changed([&, next = 10](auto) mutable { calls.push_back(++next); });
    });
    ASSERT_TRUE(destination.set({"first"}));
    ASSERT_TRUE(destination.set({"second"}));
    ASSERT_TRUE(destination.clear_if({"second"}));
    EXPECT_EQ(calls, (std::vector{1, 11, 12}));

    destination.on_changed({});
    ASSERT_TRUE(destination.set({"third"}));
    EXPECT_EQ(calls.size(), 3U);
}

TEST(PlaylistDestination, TheObserverMayDisposeTheDestinationDuringPublication)
{
    auto destination = std::make_unique<wiola::app::PlaylistDestination>();
    std::optional<wiola::library::PlaylistId> observed;
    destination->on_changed([&](auto value) {
        destination.reset();
        observed = std::move(value);
    });
    EXPECT_TRUE(destination->set({"retained-value"}));
    EXPECT_FALSE(destination);
    EXPECT_EQ(observed, wiola::library::PlaylistId{"retained-value"});
}
