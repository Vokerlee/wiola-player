/**
 * @file
 * @brief Owned source attachment and quiescent rebind contracts.
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

#include <buffering/buffer_source.hpp>
#include <buffering/pcm_queue.hpp>
#include <fakes/decoder.hpp>
#include <fakes/output.hpp>
#include <pipeline/output_session.hpp>
#include <pipeline/playback_pipeline.hpp>
#include <wiola/audio/processing_chain.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <optional>
#include <semaphore>
#include <thread>

namespace {

using wiola::audio::Output;
using wiola::core::Lease;
using wiola::engine::OutputSession;
using wiola::engine::PlaybackPipeline;
using wiola::pcm::Frames;
using wiola::pcm::StreamSpec;

constexpr StreamSpec stereo{wiola::units::Frequency{48000}, 2};
constexpr StreamSpec mono{wiola::units::Frequency{44100}, 1};

Lease<wiola::audio::BufferSource> source_for(StreamSpec spec)
{
    const auto queue = Lease<wiola::pcm::PcmQueue>::make(spec, Frames{8});
    return Lease<wiola::audio::BufferSource>::dependent(
        [spec](wiola::pcm::PcmQueue& queue) {
            return std::make_unique<wiola::audio::BufferSource>(spec, queue.consumer());
        },
        queue
    );
}

constexpr auto passive_output = [](wiola::audio::Source&) {
    return std::make_unique<wiola::testing::FakeOutput>();
};

TEST(SourceAttachment, WrongCachePermissionOrShapeDoesNotChangeTheBinding)
{
    const auto stage = Lease<wiola::audio::ProcessingChain>::make();
    OutputSession owner{stage, passive_output};
    OutputSession other{stage, passive_output};
    auto attachment = owner.prepare(stereo);
    auto permission = attachment->take_quiescence();
    ASSERT_TRUE(permission);
    const auto source = source_for(stereo);
    const auto wrong_source = source_for(mono);
    const auto unrelated_output = Lease<wiola::testing::FakeOutput>::make();
    auto unrelated = Output::quiesce(unrelated_output);
    ASSERT_TRUE(unrelated);

    EXPECT_FALSE(other.activate(*attachment, source, *permission));
    EXPECT_FALSE(owner.activate(*attachment, source, *unrelated));
    EXPECT_FALSE(owner.activate(*attachment, wrong_source, *permission));
    EXPECT_FALSE(owner.open_for(stereo));
    EXPECT_FALSE(other.open_for(stereo));
    EXPECT_TRUE(permission->matches(attachment->output()));

    EXPECT_TRUE(owner.activate(*attachment, source, *permission));
    EXPECT_TRUE(owner.open_for(stereo));
}

TEST(SourceAttachment, ReconstructingTheCacheCannotAdoptAnOlderCachesAttachment)
{
    const auto stage = Lease<wiola::audio::ProcessingChain>::make();
    std::optional<OutputSession> owner;
    owner.emplace(stage, passive_output);
    auto attachment = owner->prepare(stereo);
    auto permission = attachment->take_quiescence();
    ASSERT_TRUE(permission);
    owner.reset();
    owner.emplace(stage, passive_output);

    EXPECT_FALSE(owner->activate(*attachment, source_for(stereo), *permission));
    EXPECT_FALSE(owner->open_for(stereo));
}

TEST(SourceAttachment, BindingRetainsItsSourceAndQueueUntilStoppedRetirement)
{
    const auto stage = Lease<wiola::audio::ProcessingChain>::make();
    OutputSession owner{stage, passive_output};
    auto attachment = owner.prepare(stereo);
    wiola::core::WeakLease<wiola::pcm::PcmQueue> queue_observer;
    wiola::core::WeakLease<wiola::audio::BufferSource> source_observer;
    {
        const auto queue = Lease<wiola::pcm::PcmQueue>::make(stereo, Frames{8});
        const auto source = Lease<wiola::audio::BufferSource>::dependent(
            [](wiola::pcm::PcmQueue& queue) {
                return std::make_unique<wiola::audio::BufferSource>(stereo, queue.consumer());
            },
            queue
        );
        queue_observer = queue.weak();
        source_observer = source.weak();
        auto permission = attachment->take_quiescence();
        ASSERT_TRUE(permission);
        ASSERT_TRUE(owner.activate(*attachment, source, *permission));
    }
    EXPECT_TRUE(queue_observer.lock());
    EXPECT_TRUE(source_observer.lock());

    attachment.reset();
    EXPECT_FALSE(source_observer.lock());
    EXPECT_FALSE(queue_observer.lock());
    EXPECT_TRUE(owner.open_for(stereo));
}

TEST(SourceAttachment, AControllerCanOutliveTheAttachmentWithoutFreeingItsInflightSource)
{
    struct Gates {
        std::binary_semaphore entered{0};
        std::binary_semaphore release{0};
    } gates;

    class BlockingSource final : public wiola::audio::Source {
    public:
        explicit BlockingSource(Gates& gates)
            : gates_{gates}
        {
        }

        StreamSpec spec() const noexcept override { return stereo; }

        std::size_t render(std::span<float>) noexcept override
        {
            // The test owns the gates independently, so even a premature source-retirement
            // regression can be reported and unblocked without stranding the stopping thread.
            auto& gates = gates_;
            gates.entered.release();
            gates.release.acquire();
            return 0;
        }

    private:
        Gates& gates_;
    };

    const auto stage = Lease<wiola::audio::ProcessingChain>::make();
    wiola::testing::FakeOutput* device{};
    OutputSession owner{stage, [&](wiola::audio::Source& source) {
                            auto output = std::make_unique<wiola::testing::FakeOutput>(source);
                            device = output.get();
                            return output;
                        }};
    auto attachment = owner.prepare(stereo);
    wiola::core::WeakLease<BlockingSource> observed;
    {
        const auto source = Lease<BlockingSource>::make(gates);
        observed = source.weak();
        auto permission = attachment->take_quiescence();
        ASSERT_TRUE(permission);
        ASSERT_TRUE(owner.activate(*attachment, source, *permission));
    }
    auto controller = Output::acquire(attachment->output());
    ASSERT_TRUE(controller.start());
    gates.entered.acquire();

    attachment.reset();
    EXPECT_TRUE(observed.lock());
    EXPECT_THROW(static_cast<void>(owner.prepare(stereo)), OutputSession::Unavailable);

    std::optional<Output::Quiescence> permission;
    std::binary_semaphore issued{0};
    std::jthread stopping{[&] {
        permission = Output::quiesce(std::move(controller));
        issued.release();
    }};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    while (device->running() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();
    EXPECT_FALSE(device->running());
    const bool issued_early = issued.try_acquire();
    EXPECT_FALSE(issued_early);
    gates.release.release();
    if (!issued_early)
        issued.acquire();
    stopping.join();
    ASSERT_TRUE(permission);
    EXPECT_TRUE(observed.lock());

    permission.reset();
    const auto next = owner.prepare(stereo);
    EXPECT_FALSE(observed.lock());
}

TEST(PlaybackPipeline, RebindingRequiresThePriorControllersStoppedPermission)
{
    const auto stage = Lease<wiola::audio::ProcessingChain>::make();
    OutputSession output{stage, passive_output};
    PlaybackPipeline active{std::make_unique<wiola::testing::FakeDecoder>(Frames{64}, stereo),
        output.prepare(stereo), Frames{64}};
    ASSERT_TRUE(active.activate(output));
    PlaybackPipeline candidate{std::make_unique<wiola::testing::FakeDecoder>(Frames{64}, stereo),
        output.prepare(stereo), Frames{64}};

    EXPECT_FALSE(candidate.activate(output));
    const auto unrelated = Lease<wiola::testing::FakeOutput>::make();
    auto wrong = Output::quiesce(unrelated);
    ASSERT_TRUE(wrong);
    EXPECT_FALSE(candidate.activate(output, std::move(wrong)));

    auto stopped = active.quiesce_output();
    ASSERT_TRUE(stopped);
    EXPECT_TRUE(candidate.activate(output, std::move(stopped)));
}

TEST(PlaybackPipeline, AnIncompatiblePreparedStreamCannotBeActivated)
{
    const auto stage = Lease<wiola::audio::ProcessingChain>::make();
    OutputSession output{stage, passive_output};
    PlaybackPipeline candidate{std::make_unique<wiola::testing::FakeDecoder>(Frames{64}, stereo),
        output.prepare(mono), Frames{64}};

    EXPECT_FALSE(candidate.activate(output));
    EXPECT_FALSE(output.open_for(stereo));
    EXPECT_FALSE(output.open_for(mono));
}

} // namespace

TEST(PlaybackPipeline, APreviouslyStartedCandidateCannotRebindADevice)
{
    const auto stage = Lease<wiola::audio::ProcessingChain>::make();
    OutputSession output{stage, passive_output};
    PlaybackPipeline candidate{std::make_unique<wiola::testing::FakeDecoder>(Frames{64}, stereo),
        output.prepare(stereo), Frames{64}};
    ASSERT_TRUE(candidate.player().start());
    candidate.player().wait();

    EXPECT_FALSE(candidate.activate(output));
    EXPECT_FALSE(output.open_for(stereo));
}
