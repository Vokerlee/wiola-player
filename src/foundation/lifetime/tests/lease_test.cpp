/**
 * @file
 * @brief Typed lifetime graph construction, projections and teardown contracts.
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

#include <wiola/foundation/lease.hpp>
#include <wiola/foundation/lease_handle.hpp>

#include <gtest/gtest.h>

#include <array>
#include <concepts>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using wiola::core::Lease;
using wiola::core::WeakLease;

template<typename T>
concept ReadsTemporary = requires(T value) { std::move(value).get(); };

template<typename Factory>
concept MakesInteger = requires(Factory factory) { Lease<int>::dependent(factory); };

static_assert(!std::default_initializable<Lease<int>>);
static_assert(!std::constructible_from<Lease<int>, int&>);
static_assert(!std::constructible_from<Lease<int>, std::shared_ptr<int>>);
static_assert(!ReadsTemporary<Lease<int>>);
static_assert(!std::constructible_from<Lease<int>, Lease<const int>>);
static_assert(std::is_nothrow_copy_constructible_v<Lease<int>>);
static_assert(!MakesInteger<decltype([] { return static_cast<int*>(nullptr); })>);

struct Tracked {
    explicit Tracked(std::shared_ptr<std::vector<int>> log, int id)
        : log{std::move(log)}
        , id{id}
    {
    }

    ~Tracked() { log->push_back(id); }

    std::shared_ptr<std::vector<int>> log;
    int id;
};

TEST(LifetimeGraph, CopiesAndRvalueCopiesAlwaysRetainAUsableNode)
{
    auto original = Lease<int>::make(7);
    auto copy = original;
    auto from_rvalue = std::move(original);
    EXPECT_EQ(original.get(), 7);
    copy.get() = 9;
    EXPECT_EQ(from_rvalue.get(), 9);
    EXPECT_EQ(&copy.get(), &original.get());

    copy = original;
    const Lease<const int> immutable = original;
    EXPECT_EQ(immutable.get(), 9);
}

TEST(LifetimeGraph, DependentObjectsDieBeforeTheirRetainedInputs)
{
    auto log = std::make_shared<std::vector<int>>();
    std::optional<Lease<Tracked>> input = Lease<Tracked>::make(log, 2);
    auto output = Lease<Tracked>::dependent(
        [log](Tracked& dependency) {
            EXPECT_EQ(dependency.id, 2);
            return std::make_unique<Tracked>(log, 1);
        },
        *input
    );
    const auto weak = input->weak();
    input.reset();
    EXPECT_TRUE(log->empty());
    EXPECT_TRUE(weak.lock());

    {
        std::optional<Lease<Tracked>> last = output;
        output = Lease<Tracked>::make(log, 3);
        EXPECT_TRUE(log->empty());
        last.reset();
    }
    EXPECT_EQ(*log, (std::vector<int>{1, 2}));
    EXPECT_FALSE(weak.lock());
}

TEST(LifetimeGraph, WeakObservationPinsTheObjectAcrossItsOriginalOwnerRelease)
{
    WeakLease<int> empty;
    EXPECT_FALSE(empty.lock());
    std::optional<Lease<int>> original = Lease<int>::make(42);
    const auto observer = original->weak();
    auto active = observer.lock();
    ASSERT_TRUE(active);
    original.reset();
    EXPECT_EQ(active->get(), 42);
    EXPECT_TRUE(observer.lock());

    active.reset();
    EXPECT_FALSE(observer.lock());
}

TEST(LifetimeGraph, DirectMemberViewsRetainTheParentAndRespectConstness)
{
    struct Parent {
        int value = 7;
        std::array<int, 2> samples{1, 2};
    };

    std::optional<Lease<Parent>> parent = Lease<Parent>::make();
    auto value = parent->member<&Parent::value>();
    const Lease<const Parent> immutable = *parent;
    const auto samples = immutable.member<&Parent::samples>();
    static_assert(std::same_as<decltype(samples), const Lease<const std::array<int, 2>>>);
    parent.reset();

    value.get() = 19;
    EXPECT_EQ(immutable->value, 19);
    EXPECT_EQ(samples.get()[1], 2);
}

TEST(LifetimeGraph, FactoryFailureAndNullResultsPreserveTheExistingGraph)
{
    auto input = Lease<int>::make(11);
    const auto factory = [](int& value) -> std::unique_ptr<int> {
        if (value < 0)
            throw std::runtime_error{"construction failed"};

        if (value == 0)
            return {};

        return std::make_unique<int>(value);
    };

    auto result = Lease<int>::dependent(factory, input);
    EXPECT_EQ(*result, 11);

    input.get() = -1;
    EXPECT_THROW(static_cast<void>(Lease<int>::dependent(factory, input)), std::runtime_error);

    input.get() = 0;
    EXPECT_THROW(static_cast<void>(Lease<int>::dependent(factory, input)), std::invalid_argument);
    EXPECT_EQ(input.get(), 0);
    EXPECT_EQ(result.get(), 11);
}

TEST(LifetimeGraph, MultipleDependenciesAndMoveOnlyFactoriesAreSupported)
{
    auto first = Lease<int>::make(3);
    auto second = Lease<int>::make(4);
    auto result = Lease<int>::dependent([extra = std::make_unique<int>(5)](
                                            int& left, int& right
                                        ) { return std::make_unique<int>(left + right + *extra); },
        first, second);
    EXPECT_EQ(result.get(), 12);
}

TEST(LifetimeGraph, InterfaceViewsKeepTheConcreteFactoryDeleter)
{
    struct Base {
        int value = 7;
    };

    struct Derived : Base {
        explicit Derived(bool& destroyed)
            : destroyed{destroyed}
        {
        }

        ~Derived() { destroyed = true; }

        bool& destroyed;
    };

    bool destroyed = false;
    {
        auto object = Lease<Base>::dependent([&] { return std::make_unique<Derived>(destroyed); });
        EXPECT_EQ(object->value, 7);
        const Lease<const Base> view = object;
        EXPECT_EQ(view->value, 7);
    }
    EXPECT_TRUE(destroyed);
}

class Handle final : public wiola::core::LeaseHandle<int> {
public:
    Handle() = default;

    explicit Handle(Lease<int> value)
        : LeaseHandle{value}
    {
    }

    int read() const { return owner() ? *owner() : -1; }

    auto retain() const { return retain_owner(); }

    void reset() { clear(); }
};

TEST(LeaseHandle, MovesTransferTheCapabilityAndClearTheSource)
{
    Handle empty;
    EXPECT_FALSE(empty);
    EXPECT_EQ(empty.read(), -1);
    EXPECT_FALSE(empty.retain());
    Handle first{Lease<int>::make(7)};
    auto second = std::move(first);
    EXPECT_FALSE(first);
    EXPECT_EQ(second.read(), 7);

    empty = std::move(second);
    EXPECT_FALSE(second);
    EXPECT_EQ(empty.read(), 7);
    Handle& self = empty;
    empty = std::move(self);
    EXPECT_EQ(empty.read(), 7);

    auto retained = empty.retain();
    empty.reset();
    EXPECT_FALSE(empty);
    ASSERT_TRUE(retained);
    EXPECT_EQ(retained->get(), 7);
}

} // namespace
