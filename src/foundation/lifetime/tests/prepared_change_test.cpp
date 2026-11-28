/**
 * @file
 * @brief Owned preparation, version isolation and reentrant commit contracts.
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
#include <wiola/foundation/prepared_change.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace {

using wiola::core::ChangeVersion;
using wiola::core::Lease;
using wiola::core::PreparedChange;

struct Owner {
    int value{7};
    ChangeVersion version;
    ChangeVersion independent;
};

constexpr auto apply_value = [](Owner& owner, int& candidate) noexcept {
    std::swap(owner.value, candidate);
};

static_assert(!std::is_move_constructible_v<ChangeVersion>);
static_assert(!std::is_copy_constructible_v<PreparedChange<Owner, int>>);
static_assert(!std::is_move_constructible_v<PreparedChange<Owner, int>>);

TEST(PreparedChange, CommitsOnceWithoutAllocatingAndRetainsTheRetiredValue)
{
    auto owner = Lease<Owner>::make();
    PreparedChange change{owner, &Owner::version, 42, apply_value};
    ASSERT_TRUE(change.ready());
    EXPECT_EQ(owner->value, 7);
    EXPECT_EQ(change.candidate(), 42);

    bool committed = false;
#if WIOLA_TEST_ALLOCATOR_REPLACEMENT
    {
        const wiola::testing::AllocationFailure failure{0};
        committed = change.commit();
    }
#else
    committed = change.commit();
#endif

    EXPECT_TRUE(committed);
    EXPECT_FALSE(change.ready());
    EXPECT_FALSE(change.commit());
    EXPECT_EQ(owner->value, 42);
    EXPECT_EQ(change.candidate(), 7);
}

TEST(PreparedChange, DiscardPreservesTheOwnerAndReleasesOwnedCandidatesBeforeIt)
{
    struct Candidate {
        explicit Candidate(Owner& owner)
            : owner{owner}
        {
        }

        ~Candidate() { EXPECT_EQ(owner.value, 7); }

        Owner& owner;
    };

    wiola::core::WeakLease<Owner> observed;
    {
        auto owner = Lease<Owner>::make();
        observed = owner.weak();
        auto change = std::make_unique<PreparedChange<Owner, std::unique_ptr<Candidate>>>(owner,
            &Owner::version, std::make_unique<Candidate>(owner.get()),
            [](Owner&, auto&) noexcept { });
        owner = Lease<Owner>::make();
        ASSERT_TRUE(observed.lock());
        EXPECT_TRUE(change->ready());
    }
    EXPECT_FALSE(observed.lock());
}

TEST(PreparedChange, InterveningCommitRejectsOlderCandidatesWithoutChangingCurrentState)
{
    const auto owner = Lease<Owner>::make();
    PreparedChange older{owner, &Owner::version, 11, apply_value};
    {
        const PreparedChange discarded{owner, &Owner::version, 99, apply_value};
        EXPECT_TRUE(older.ready());
    }
    PreparedChange newer{owner, &Owner::version, 42, apply_value};
    ASSERT_TRUE(newer.commit());

    EXPECT_FALSE(older.ready());
    EXPECT_FALSE(older.commit());
    EXPECT_EQ(owner->value, 42);
}

TEST(PreparedChange, VersionsAreIndependentAcrossOwnersAndDomains)
{
    const auto first = Lease<Owner>::make();
    const auto second = Lease<Owner>::make();
    PreparedChange pending{first, &Owner::version, 42, apply_value};
    PreparedChange unrelated{second, &Owner::version, 21, apply_value};
    PreparedChange independent{first, &Owner::independent, 17, apply_value};
    ASSERT_TRUE(unrelated.commit());
    ASSERT_TRUE(independent.commit());
    EXPECT_TRUE(pending.ready());
    EXPECT_TRUE(pending.commit());
    EXPECT_EQ(first->value, 42);
    EXPECT_EQ(second->value, 21);
}

TEST(PreparedChange, ReentrantCommitCannotApplyAnotherCandidateToTheSameDomain)
{
    const auto owner = Lease<Owner>::make();
    PreparedChange sibling{owner, &Owner::version, 21, apply_value};
    bool nested_ready = true;
    bool nested_commit = true;
    PreparedChange outer{owner, &Owner::version, 42, [&](Owner& owner, int& value) noexcept {
                             nested_ready = sibling.ready();
                             nested_commit = sibling.commit();
                             owner.value = value;
                         }};

    EXPECT_TRUE(outer.commit());
    EXPECT_FALSE(nested_ready);
    EXPECT_FALSE(nested_commit);
    EXPECT_FALSE(sibling.ready());
    EXPECT_EQ(owner->value, 42);
}

TEST(PreparedChange, ActiveCommitSurvivesItsOwnTransactionAndOriginalOwnerDisposal)
{
    std::unique_ptr<PreparedChange<Owner, int>> change;
    wiola::core::WeakLease<Owner> observed;
    {
        const auto owner = Lease<Owner>::make();
        observed = owner.weak();
        change = std::make_unique<PreparedChange<Owner, int>>(owner, &Owner::version, 42,
            [&](Owner& owner, int& value) noexcept {
                change.reset();
                EXPECT_TRUE(observed.lock());
                owner.value = value;
            });
    }
    ASSERT_TRUE(change->commit());
    EXPECT_FALSE(change);
    EXPECT_FALSE(observed.lock());
}

TEST(PreparedChange, InvalidSetupAndCandidateFailureDoNotInvalidateExistingPreparations)
{
    const auto owner = Lease<Owner>::make();
    PreparedChange pending{owner, &Owner::version, 42, apply_value};
    using Apply = void (*)(Owner&, int&) noexcept;
    EXPECT_THROW((PreparedChange{owner, &Owner::version, 7, Apply{nullptr}}),
        std::invalid_argument);
    EXPECT_THROW((PreparedChange{owner, static_cast<ChangeVersion Owner::*>(nullptr), 7,
                     apply_value}),
        std::invalid_argument);

    struct FailingCandidate {
        FailingCandidate() = default;

        FailingCandidate(FailingCandidate&&) { throw std::runtime_error{"candidate move failed"}; }
    };

    EXPECT_THROW((PreparedChange{owner, &Owner::version, FailingCandidate{},
                     [](Owner&, FailingCandidate&) noexcept { }}),
        std::runtime_error);

    EXPECT_TRUE(pending.ready());
    EXPECT_TRUE(pending.commit());
    EXPECT_EQ(owner->value, 42);
}

} // namespace
