/**
 * @file
 * @brief Retained stage preparation and private-driver activation contracts.
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
#include <wiola/audio/stage.hpp>

#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <type_traits>

namespace {

using wiola::audio::Stage;
using wiola::core::Lease;
using wiola::pcm::StreamSpec;

struct Probe {
    unsigned created{};
    unsigned retired{};
    unsigned applied{};
    unsigned destroyed{};
    StreamSpec spec{};
    std::function<void()> on_apply;
};

class ControlledStage final : public Stage {
public:
    explicit ControlledStage(std::shared_ptr<Probe> probe)
        : probe_{std::move(probe)}
    {
    }

    ~ControlledStage() override { ++probe_->destroyed; }

    void process(std::span<float>) noexcept override { }

    bool reject{false};

private:
    class Candidate final : public Preparation {
    public:
        Candidate(ControlledStage& owner, StreamSpec spec)
            : owner_{owner}
            , spec_{spec}
            , accepted_{!owner.reject}
        {
            ++owner_.probe_->created;
        }

        ~Candidate() override { ++owner_.probe_->retired; }

    private:
        bool can_activate() const noexcept override { return accepted_; }

        void activate() noexcept override
        {
            if (owner_.probe_->on_apply)
                owner_.probe_->on_apply();
            ++owner_.probe_->applied;
            owner_.probe_->spec = spec_;
        }

        ControlledStage& owner_;
        StreamSpec spec_;
        bool accepted_;
    };

    std::unique_ptr<Preparation> prepare_change(StreamSpec spec) override
    {
        return std::make_unique<Candidate>(*this, spec);
    }

    std::shared_ptr<Probe> probe_;
};

static_assert(!std::is_invocable_v<decltype(&Stage::prepare), Stage&, StreamSpec>);

TEST(StagePreparation, RetainsTheStageAndItsPrivateCandidateAfterTheOriginalOwnerIsReleased)
{
    auto probe = std::make_shared<Probe>();
    wiola::core::WeakLease<ControlledStage> observed;
    std::unique_ptr<Stage::Preparation> prepared;
    {
        const auto stage = Lease<ControlledStage>::make(probe);
        observed = stage.weak();
        prepared = Stage::prepare(stage, {});
    }

    ASSERT_TRUE(prepared);
    EXPECT_TRUE(observed.lock());
    EXPECT_EQ(probe->applied, 0U);
    EXPECT_TRUE(prepared->commit());
    EXPECT_FALSE(prepared->commit());
    EXPECT_EQ(probe->applied, 1U);
    EXPECT_EQ(probe->destroyed, 0U);

    prepared.reset();
    EXPECT_EQ(probe->retired, probe->created);
    EXPECT_EQ(probe->destroyed, 1U);
    EXPECT_FALSE(observed.lock());
}

TEST(StagePreparation, DiscardPreservesTheConfigurationAndRetiresTheCandidateBeforeItsOwner)
{
    auto probe = std::make_shared<Probe>();
    std::unique_ptr<Stage::Preparation> prepared;
    {
        const auto stage = Lease<ControlledStage>::make(probe);
        prepared = Stage::prepare(stage, {.sample_rate = wiola::units::Frequency{22050}});
    }

    prepared.reset();
    EXPECT_EQ(probe->applied, 0U);
    EXPECT_EQ(probe->spec, StreamSpec{});
    EXPECT_EQ(probe->retired, probe->created);
    EXPECT_EQ(probe->destroyed, 1U);
}

TEST(StagePreparation, LaterCommitRejectsAnOlderCustomDriverCandidate)
{
    auto probe = std::make_shared<Probe>();
    const auto stage = Lease<ControlledStage>::make(probe);
    auto old = Stage::prepare(stage, {});
    const StreamSpec replacement{.sample_rate = wiola::units::Frequency{22050}};
    auto current = Stage::prepare(stage, replacement);
    ASSERT_TRUE(current->commit());

    EXPECT_FALSE(old->ready());
    EXPECT_FALSE(old->commit());
    EXPECT_EQ(probe->applied, 1U);
    EXPECT_EQ(probe->spec, replacement);
}

TEST(StagePreparation, ImmediateConfigurationReportsAnUnavailableCandidate)
{
    auto probe = std::make_shared<Probe>();
    const auto stage = Lease<ControlledStage>::make(probe);
    stage->reject = true;
    EXPECT_THROW(Stage::configure(stage, {}), std::logic_error);
    EXPECT_EQ(probe->applied, 0U);

    stage->reject = false;
    Stage::configure(stage, {});
    EXPECT_EQ(probe->applied, 1U);
}

TEST(StagePreparation, TheCompleteRetainedCommitDoesNotAllocate)
{
    auto probe = std::make_shared<Probe>();
    const auto stage = Lease<ControlledStage>::make(probe);
    auto prepared = Stage::prepare(stage, {});
    bool applied = false;
#if WIOLA_TEST_ALLOCATOR_REPLACEMENT
    {
        const wiola::testing::AllocationFailure failure{0};
        applied = prepared->commit();
    }
#else
    applied = prepared->commit();
#endif
    EXPECT_TRUE(applied);
    EXPECT_EQ(probe->applied, 1U);
}

} // namespace

TEST(StagePreparation, ActiveDriverCommitSurvivesReentrantDisposalOfItsRetainedWrapper)
{
    auto probe = std::make_shared<Probe>();
    std::unique_ptr<Stage::Preparation> prepared;
    wiola::core::WeakLease<ControlledStage> observed;
    {
        const auto stage = Lease<ControlledStage>::make(probe);
        observed = stage.weak();
        prepared = Stage::prepare(stage, {});
    }
    probe->on_apply = [&] {
        prepared.reset();
        EXPECT_TRUE(observed.lock());
        EXPECT_EQ(probe->destroyed, 0U);
    };

    EXPECT_TRUE(prepared->commit());
    EXPECT_FALSE(prepared);
    EXPECT_FALSE(observed.lock());
    EXPECT_EQ(probe->applied, 1U);
    EXPECT_EQ(probe->retired, probe->created);
    EXPECT_EQ(probe->destroyed, 1U);
}
