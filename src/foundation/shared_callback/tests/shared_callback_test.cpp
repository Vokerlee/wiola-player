/**
 * @file
 * @brief Independent shared callback ownership and invocation contracts.
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

#include <wiola/foundation/shared_callback.hpp>

#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace {

using wiola::core::SharedCallback;

static_assert(std::is_nothrow_default_constructible_v<SharedCallback<void()>>);
static_assert(std::is_nothrow_copy_constructible_v<SharedCallback<void()>>);
static_assert(std::is_nothrow_move_constructible_v<SharedCallback<void()>>);
static_assert(std::is_nothrow_copy_assignable_v<SharedCallback<void()>>);
static_assert(std::is_nothrow_move_assignable_v<SharedCallback<void()>>);

TEST(SharedCallback, EmptyFunctionsRemainEmptyAndRejectInvocation)
{
    SharedCallback<int()> empty;
    SharedCallback<int()> from_function{std::function<int()>{}};
    EXPECT_FALSE(empty);
    EXPECT_FALSE(from_function);
    EXPECT_THROW(empty(), std::bad_function_call);
    EXPECT_THROW(from_function(), std::bad_function_call);
}

TEST(SharedCallback, CopiesShareMutableStateAfterTheOriginalIsCleared)
{
    SharedCallback<int()> original{[next = 0]() mutable { return ++next; }};
    auto retained = original;
    const auto another = retained;
    EXPECT_EQ(original(), 1);
    EXPECT_EQ(retained(), 2);

    original = {};
    EXPECT_FALSE(original);
    EXPECT_EQ(another(), 3);
    EXPECT_EQ(retained(), 4);
}

TEST(SharedCallback, MovesTransferTheTargetAndClearTheSource)
{
    int calls = 0;
    SharedCallback<void()> original{[&] { ++calls; }};
    auto moved = std::move(original);
    EXPECT_FALSE(original);
    ASSERT_TRUE(moved);
    moved();

    SharedCallback<void()> assigned;
    assigned = std::move(moved);
    EXPECT_FALSE(moved);
    ASSERT_TRUE(assigned);
    assigned();
    EXPECT_EQ(calls, 2);
}

TEST(SharedCallback, ActiveTargetSurvivesReplacingItsOwnContainer)
{
    auto state = std::make_shared<int>(7);
    const std::weak_ptr<int> lifetime = state;
    SharedCallback<int()> callback;
    callback = SharedCallback<int()>{[&, keep = std::move(state)] {
        callback = SharedCallback<int()>{[] { return 11; }};
        EXPECT_FALSE(lifetime.expired());
        return *keep;
    }};

    EXPECT_EQ(callback(), 7);
    EXPECT_TRUE(lifetime.expired());
    EXPECT_EQ(callback(), 11);
}

TEST(SharedCallback, ActiveTargetSurvivesDestructionOfItsOwningObject)
{
    struct Owner {
        SharedCallback<void()> notify;
    };

    auto owner = std::make_unique<Owner>();
    auto state = std::make_shared<int>(42);
    const std::weak_ptr<int> lifetime = state;
    int observed = 0;
    owner->notify = SharedCallback<void()>{[&, keep = std::move(state)] {
        owner.reset();
        EXPECT_FALSE(lifetime.expired());
        observed = *keep;
    }};

    owner->notify();
    EXPECT_EQ(owner, nullptr);
    EXPECT_EQ(observed, 42);
    EXPECT_TRUE(lifetime.expired());
}

TEST(SharedCallback, ForwardsReferencesAndMoveOnlyArgumentsAndResults)
{
    SharedCallback<std::unique_ptr<int>(std::unique_ptr<int>, int&)> callback{
        [](std::unique_ptr<int> value, int& total) {
            total += *value;
            return value;
        }};
    auto input = std::make_unique<int>(9);
    int total = 1;
    const auto output = callback(std::move(input), total);
    ASSERT_NE(output, nullptr);
    EXPECT_EQ(*output, 9);
    EXPECT_EQ(input, nullptr);
    EXPECT_EQ(total, 10);

    SharedCallback<int&()> reference{[&]() -> int& { return total; }};
    reference() = 17;
    EXPECT_EQ(total, 17);
}

TEST(SharedCallback, CallbackExceptionsPropagateWithoutDiscardingItsState)
{
    SharedCallback<void()> callback{[attempt = 0]() mutable {
        if (attempt++ == 0)
            throw std::runtime_error{"first attempt"};
    }};
    EXPECT_THROW(callback(), std::runtime_error);
    EXPECT_NO_THROW(callback());
    EXPECT_TRUE(callback);
}

TEST(SharedCallback, CapturesRetireWhenTheLastContainerAndInvocationReleaseThem)
{
    auto state = std::make_shared<int>(1);
    const std::weak_ptr<int> lifetime = state;
    SharedCallback<void()> first{[keep = std::move(state)] { static_cast<void>(*keep); }};
    auto last = first;
    first = {};
    EXPECT_FALSE(lifetime.expired());

    last();
    last = {};
    EXPECT_TRUE(lifetime.expired());
}

} // namespace
