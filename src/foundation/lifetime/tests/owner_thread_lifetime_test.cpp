/**
 * @file
 * @brief Owner-thread invalidation and reentrant observation contracts.
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

#include <wiola/foundation/owner_thread_lifetime.hpp>

#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <thread>
#include <type_traits>

namespace {

using wiola::core::OwnerThreadLifetime;

static_assert(!std::is_copy_constructible_v<OwnerThreadLifetime<int>>);
static_assert(!std::is_move_constructible_v<OwnerThreadLifetime<int>>);
static_assert(!std::is_constructible_v<OwnerThreadLifetime<int>, int&&>);
static_assert(!std::is_constructible_v<OwnerThreadLifetime<const int>, int&&>);
static_assert(!std::is_constructible_v<OwnerThreadLifetime<const int>, const int&&>);
static_assert(std::is_constructible_v<OwnerThreadLifetime<const int>, const int&>);

TEST(OwnerThreadLifetime, EmptyAndInvalidatedObservationsExposeNoOwner)
{
    OwnerThreadLifetime<int>::Observation observation;
    EXPECT_EQ(observation.get(), nullptr);

    int value = 7;
    {
        OwnerThreadLifetime lifetime{value};
        observation = lifetime.observe();
        EXPECT_EQ(observation.get(), &value);

        lifetime.invalidate();
        lifetime.invalidate();
        EXPECT_EQ(observation.get(), nullptr);
        EXPECT_EQ(lifetime.observe().get(), nullptr);
    }
    EXPECT_EQ(observation.get(), nullptr);
}

TEST(OwnerThreadLifetime, ScopeDestructionInvalidatesEveryObservationCopy)
{
    int value = 11;
    OwnerThreadLifetime<int>::Observation observation;
    {
        OwnerThreadLifetime lifetime{value};
        observation = lifetime.observe();
    }

    const auto copy = observation;
    EXPECT_EQ(copy.get(), nullptr);
    EXPECT_EQ(observation.get(), nullptr);
}

TEST(OwnerThreadLifetime, OwnerTeardownCanInvalidateBeforeReentrantCallbacks)
{
    struct Owner {
        ~Owner()
        {
            lifetime.invalidate();
            closing();
        }

        OwnerThreadLifetime<Owner> lifetime{*this};
        std::function<void()> closing;
    };

    auto owner = std::make_unique<Owner>();
    const auto observation = owner->lifetime.observe();
    owner->closing = [observation] {
        EXPECT_EQ(observation.get(), nullptr);
    };
    ASSERT_EQ(observation.get(), owner.get());

    owner.reset();
    EXPECT_EQ(observation.get(), nullptr);
}

TEST(OwnerThreadLifetime, WorkersCanTransportObservationsForOwnerThreadDelivery)
{
    int value = 42;
    OwnerThreadLifetime lifetime{value};
    OwnerThreadLifetime<int>::Observation delivered;

    std::jthread worker{[&] { delivered = lifetime.observe(); }};
    worker.join();

    ASSERT_EQ(delivered.get(), &value);
    EXPECT_EQ(*delivered.get(), 42);
}

#ifndef NDEBUG
TEST(OwnerThreadLifetimeDeathTest, RejectsAccessOnAnotherThread)
{
    int value = 7;
    OwnerThreadLifetime lifetime{value};
    const auto observation = lifetime.observe();

    EXPECT_DEATH(
        { std::jthread worker{[observation] { static_cast<void>(observation.get()); }}; },
        "owner thread"
    );
}

TEST(OwnerThreadLifetimeDeathTest, RejectsInvalidationOnAnotherThread)
{
    int value = 7;
    OwnerThreadLifetime lifetime{value};

    EXPECT_DEATH({ std::jthread worker{[&] { lifetime.invalidate(); }}; }, "owner thread");
}
#endif

} // namespace
