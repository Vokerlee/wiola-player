/**
 * @file
 * @brief Tests for what is done to samples on their way to the output.
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
#include <fakes/tone.hpp>
#include <fixtures/shaping.hpp>
#include <wiola/audio/processing_chain.hpp>
#include <wiola/audio/stage.hpp>
#include <wiola/media/stream_spec.hpp>
#include <wiola/units.hpp>

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <new>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

using wiola::audio::ProcessingChain;
using wiola::audio::Stage;
using wiola::pcm::Frames;
using wiola::pcm::StreamSpec;
using namespace wiola::units;

constexpr StreamSpec stereo{.sample_rate = 48_kHz, .num_channels = 2};

auto samples()
{
    return std::array{1.0F, -0.5F, 0.25F, 0.0F};
}

/// A step that says when it ran and multiplies by whatever it was built with.
class Marking final : public Stage {
public:
    Marking(std::vector<std::string>& ran, std::string name, float factor)
        : ran_{ran}
        , name_{std::move(name)}
        , factor_{factor}
    {
    }

    std::unique_ptr<Preparation> prepare(StreamSpec spec) override
    {
        if (fail_preparation)
            throw std::runtime_error{"controlled stage preparation failure"};
        return std::make_unique<Prepared>(*this, spec);
    }

    void process(std::span<float> samples) noexcept override
    {
        ran_.push_back(name_);

        for (float& sample : samples)
            sample *= factor_;
    }

    StreamSpec configured{};
    int num_configures{0};
    int pending_preparations{0};
    bool fail_preparation{false};

private:
    class Prepared final : public Preparation {
    public:
        Prepared(Marking& owner, StreamSpec spec) noexcept
            : owner_{owner}
            , spec_{spec}
        {
            ++owner_.pending_preparations;
        }

        ~Prepared() override { --owner_.pending_preparations; }

    private:
        void activate() noexcept override
        {
            owner_.configured = spec_;
            ++owner_.num_configures;
        }

        Marking& owner_;
        StreamSpec spec_;
    };

    std::vector<std::string>& ran_;
    std::string name_;
    float factor_;
};

} // namespace

TEST(ProcessingChain, LeavesSamplesAloneWithNoSteps)
{
    ProcessingChain chain;
    auto buffer{samples()};

    chain.process(buffer);

    EXPECT_EQ(buffer, samples());
}

TEST(ProcessingChain, RunsStepsInTheOrderTheyWereAdded)
{
    std::vector<std::string> ran;
    Marking first{ran, "first", 1.0F};
    Marking second{ran, "second", 1.0F};
    Marking third{ran, "third", 1.0F};

    ProcessingChain chain{
        {first, second, third}
    };

    auto buffer{samples()};
    chain.process(buffer);

    EXPECT_EQ(ran, (std::vector<std::string>{"first", "second", "third"}));
}

TEST(ProcessingChain, RunsEveryStepOverTheSamples)
{
    std::vector<std::string> ran;
    Marking first{ran, "half", 0.5F};
    Marking second{ran, "half again", 0.5F};

    ProcessingChain chain{
        {first, second}
    };

    std::array block{0.8F, 0.4F};
    chain.process(block);

    EXPECT_FLOAT_EQ(block[0], 0.2F);
    EXPECT_FLOAT_EQ(block[1], 0.1F);
}

TEST(ProcessingChain, TellsEveryStepAboutTheFormat)
{
    std::vector<std::string> ran;
    Marking first{ran, "first", 1.0F};
    Marking second{ran, "second", 1.0F};

    ProcessingChain chain{
        {first, second}
    };

    chain.configure(stereo);

    EXPECT_EQ(first.num_configures, 1);
    EXPECT_EQ(second.num_configures, 1);
    EXPECT_EQ(first.configured, stereo);
    EXPECT_EQ(second.configured, stereo);
}

/// The ceiling is the chain's own, kept after every step whatever the steps did. A step is never
/// asked whether it lifted, so a step that lifts and does not say so cannot hand over more than
/// an output takes.
TEST(ProcessingChain, HoldsTheCeilingWhateverTheStepsDid)
{
    std::vector<std::string> ran;
    Marking loud{ran, "loud", 4.0F};

    ProcessingChain chain{{loud}};

    std::array block{0.8F, -0.8F, 0.1F};
    chain.process(block);

    EXPECT_FLOAT_EQ(block[0], 1.0F);
    EXPECT_FLOAT_EQ(block[1], -1.0F);
    EXPECT_FLOAT_EQ(block[2], 0.4F);
}

/// What arrives past full scale is held to it too, whether or not anything in the chain lifted.
TEST(ProcessingChain, HoldsTheCeilingOnWhatArrivedPastIt)
{
    ProcessingChain chain;
    std::array block{2.0F, -3.0F, 0.5F};

    chain.process(block);

    EXPECT_FLOAT_EQ(block[0], 1.0F);
    EXPECT_FLOAT_EQ(block[1], -1.0F);
    EXPECT_FLOAT_EQ(block[2], 0.5F);
}

TEST(ProcessingChain, AcceptsNothingToDo)
{
    std::vector<std::string> ran;
    Marking half{ran, "half", 0.5F};

    ProcessingChain chain{{half}};
    chain.process(std::span<float>{});

    // Nothing to do is not something to recover from: what comes after is shaped as it would
    // have been.
    std::array block{1.0F, 1.0F};
    chain.process(block);

    EXPECT_FLOAT_EQ(block[0], 0.5F);
    EXPECT_FLOAT_EQ(block[1], 0.5F);
}

/// The bands first, then how loud: what a listener asks for last is the last thing done.
TEST(ProcessingChain, ShapesThenSetsHowLoud)
{
    wiola::testing::Shaping shaping{stereo};
    auto buffer{samples()};

    shaping.equalizer.set_band_gain(5, 12.0F);
    shaping.equalizer.set_enabled(false);
    shaping.volume.set_gain(0.5F);
    shaping.chain.process(buffer);

    EXPECT_FLOAT_EQ(buffer[0], 0.5F);
    EXPECT_FLOAT_EQ(buffer[1], -0.25F);
}

TEST(ProcessingChain, TakesANewFormatWithoutForgettingTheSetting)
{
    wiola::testing::Shaping shaping{stereo};

    shaping.volume.set_gain(0.5F);
    shaping.chain.configure(StreamSpec{.sample_rate = 22050_Hz, .num_channels = 1});

    EXPECT_FLOAT_EQ(shaping.volume.gain(), 0.5F);
    EXPECT_EQ(shaping.equalizer.num_bands(), 9u);

    shaping.chain.configure(stereo);

    EXPECT_EQ(shaping.equalizer.num_bands(), 10u);
}

/// A boosted band can ask for more than an output takes, and is not allowed to hand it over.
TEST(ProcessingChain, KeepsBoostedSamplesInRange)
{
    wiola::testing::Shaping shaping{stereo};
    wiola::testing::SineSource source{stereo, 1_kHz, 0.95F};
    std::vector<float> block(stereo.samples_per(Frames{512}));

    shaping.equalizer.set_band_gain(5, 12.0F);
    shaping.volume.set_gain(shaping.volume.policy().max_boost);

    for (int i = 0; i < 20; ++i) {
        source.render(block);
        shaping.chain.process(block);

        for (const float sample : block)
            ASSERT_LE(std::abs(sample), 1.0F);
    }
}

TEST(ProcessingChain, SanitizesNonFiniteOutput)
{
    ProcessingChain chain;
    std::array block{std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(), 0.25F};

    chain.process(block);

    EXPECT_EQ(block, (std::array{0.0F, 0.0F, 0.25F}));
}

TEST(ProcessingChain, DiscardedPreparationPreservesActiveStagesAndReleasesCandidates)
{
    std::vector<std::string> ran;
    Marking first{ran, "first", 1.0F};
    Marking second{ran, "second", 1.0F};
    ProcessingChain chain{
        {first, second}
    };
    chain.configure(stereo);

    const StreamSpec replacement{.sample_rate = 22050_Hz, .num_channels = 1};
    {
        const auto prepared{chain.prepare(replacement)};

        ASSERT_NE(prepared, nullptr);
        EXPECT_EQ(first.configured, stereo);
        EXPECT_EQ(second.configured, stereo);
        EXPECT_EQ(first.pending_preparations, 1);
        EXPECT_EQ(second.pending_preparations, 1);
    }
    EXPECT_EQ(first.pending_preparations, 0);
    EXPECT_EQ(second.pending_preparations, 0);
    EXPECT_EQ(first.num_configures, 1);
    EXPECT_EQ(second.num_configures, 1);
}

TEST(ProcessingChain, LaterStageFailurePreservesEarlierStagesAndReleasesCandidates)
{
    std::vector<std::string> ran;
    Marking first{ran, "first", 1.0F};
    Marking second{ran, "second", 1.0F};
    ProcessingChain chain{
        {first, second}
    };
    chain.configure(stereo);
    second.fail_preparation = true;

    EXPECT_THROW(chain.configure({.sample_rate = 22050_Hz, .num_channels = 1}), std::runtime_error);

    EXPECT_EQ(first.configured, stereo);
    EXPECT_EQ(second.configured, stereo);
    EXPECT_EQ(first.num_configures, 1);
    EXPECT_EQ(second.num_configures, 1);
    EXPECT_EQ(first.pending_preparations, 0);
    EXPECT_EQ(second.pending_preparations, 0);
}

TEST(ProcessingChain, CommitActivatesTheWholeChainOnlyOnce)
{
    std::vector<std::string> ran;
    Marking first{ran, "first", 1.0F};
    Marking second{ran, "second", 1.0F};
    ProcessingChain chain{
        {first, second}
    };
    auto prepared{chain.prepare(stereo)};

    ASSERT_NE(prepared, nullptr);
    EXPECT_EQ(first.num_configures, 0);
    EXPECT_EQ(second.num_configures, 0);

    prepared->commit();
    prepared->commit();

    EXPECT_EQ(first.num_configures, 1);
    EXPECT_EQ(second.num_configures, 1);
    EXPECT_EQ(first.configured, stereo);
    EXPECT_EQ(second.configured, stereo);
}

TEST(ProcessingChain, AChainOfStatelessStagesNeedsNoCommit)
{
    wiola::audio::Volume volume;
    ProcessingChain chain{{volume}};

    EXPECT_EQ(chain.prepare(stereo), nullptr);

    chain.configure(stereo);

    ProcessingChain empty;

    EXPECT_EQ(empty.prepare(stereo), nullptr);

    empty.configure(stereo);
}

TEST(ProcessingChain, EqualizerCommitUsesSettingsAcceptedAfterPreparation)
{
    wiola::testing::Shaping shaping{stereo};
    auto prepared{shaping.chain.prepare({.sample_rate = 22050_Hz, .num_channels = 1})};

    ASSERT_NE(prepared, nullptr);
    EXPECT_EQ(shaping.equalizer.num_bands(), 10u);

    shaping.equalizer.set_band_gain(5, 6.0F);
    shaping.equalizer.set_band_gain(9, 3.0F);
    shaping.equalizer.set_enabled(false);
    shaping.volume.set_gain(0.5F);

    prepared->commit();

    EXPECT_EQ(shaping.equalizer.num_bands(), 9u);
    EXPECT_FLOAT_EQ(shaping.equalizer.band_gain(5), 6.0F);
    EXPECT_FLOAT_EQ(shaping.equalizer.band_gain(9), 3.0F);
    EXPECT_FLOAT_EQ(shaping.equalizer.preamp(), -6.0F);
    EXPECT_FALSE(shaping.equalizer.enabled());

    std::array block{0.8F, -0.4F};
    shaping.chain.process(block);
    EXPECT_EQ(block, (std::array{0.4F, -0.2F}));
}

TEST(ProcessingChain, PreparationCanOverlapActiveEqualizerProcessing)
{
    wiola::testing::Shaping shaping{stereo};
    wiola::testing::Shaping reference{stereo};

    shaping.equalizer.set_band_gain(5, -6.0F);
    reference.equalizer.set_band_gain(5, -6.0F);

    std::array<float, 4096> seed{};
    seed.front() = 0.5F;

    auto actual{seed};
    auto expected{seed};
    std::atomic<bool> entered{false};
    std::size_t blocks{0};
    std::jthread callback{[&](std::stop_token stop) {
        entered.store(true, std::memory_order_release);
        entered.notify_one();

        do {
            actual = seed;
            shaping.chain.process(actual);
            ++blocks;
        } while (!stop.stop_requested());
    }};

    entered.wait(false, std::memory_order_acquire);

    auto prepared{shaping.chain.prepare({.sample_rate = 22050_Hz, .num_channels = 1})};

    callback.request_stop();
    callback.join();

    for (std::size_t index{0}; index < blocks; ++index) {
        expected = seed;
        reference.chain.process(expected);
    }

    EXPECT_EQ(actual, expected);
    EXPECT_EQ(shaping.equalizer.num_bands(), 10u);

    prepared->commit();
    EXPECT_EQ(shaping.equalizer.num_bands(), 9u);
}

#if WIOLA_TEST_ALLOCATOR_REPLACEMENT
TEST(ProcessingChain, AnyPreparationAllocationFailurePreservesTheWholeActiveChain)
{
    constexpr StreamSpec replacement{.sample_rate = 22050_Hz, .num_channels = 8};
    bool observed_failure{false};

    for (std::size_t after{0};; ++after) {
        wiola::audio::Equalizer first{stereo};
        wiola::audio::Equalizer second{stereo};
        ProcessingChain chain{
            {first, second}
        };

        first.set_band_gain(9, 3.0F);
        second.set_band_gain(9, 6.0F);

        bool failed{false};
        {
            const wiola::testing::AllocationFailure failure{after};
            try {
                chain.configure(replacement);
            } catch (const std::bad_alloc&) {
                failed = true;
            }
        }

        if (!failed) {
            EXPECT_TRUE(observed_failure);
            EXPECT_EQ(first.num_bands(), 9u);
            EXPECT_EQ(second.num_bands(), 9u);

            break;
        }

        observed_failure = true;
        EXPECT_EQ(first.num_bands(), 10u);
        EXPECT_EQ(second.num_bands(), 10u);
        EXPECT_FLOAT_EQ(first.preamp(), -3.0F);
        EXPECT_FLOAT_EQ(second.preamp(), -6.0F);
        EXPECT_FLOAT_EQ(first.band_gain(9), 3.0F);
        EXPECT_FLOAT_EQ(second.band_gain(9), 6.0F);
    }
}
#endif
