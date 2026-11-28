/**
 * @file
 * @brief Independent installed playback consumer using only supplied media/render ports.
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

#include <wiola/audio/output.hpp>
#include <wiola/audio/source.hpp>
#include <wiola/audio/stage.hpp>
#include <wiola/media/provider.hpp>
#include <wiola/playback/session.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string_view>
#include <thread>

namespace {

using namespace wiola;
constexpr pcm::StreamSpec stream{};
constexpr units::Time duration{std::chrono::seconds{2}};

class Decoder final : public media::AudioDecoder {
public:
    Decoder()
        : AudioDecoder{stream, stream.checked_frames_at(duration).value()}
    {
    }

private:
    media::Result<media::DecodeOutcome> decode(std::span<float> samples,
        pcm::Frames requested) override
    {
        const auto frames{std::min(requested, num_frames_left())};
        std::fill_n(samples.begin(), stream.samples_per(frames), 0.25F);

        return media::DecodeOutcome{.frames = frames, .end_of_stream = frames == num_frames_left()};
    }

    media::Result<media::SeekOutcome> seek_frame(pcm::Frames position) override
    {
        return media::SeekOutcome{.target = position, .achieved = position};
    }
};

class Asset final : public media::MediaAsset {
public:
    media::Result<media::MediaInfo> inspect(const media::OperationContext&) override
    {
        return media::unexpected{
            media::Error{media::ErrorCategory::unsupported,
                         "inspection is outside this consumer's media capability"}
        };
    }

    media::Result<media::Metadata> read_metadata(const media::OperationContext& context) override
    {
        if (context.stop_reason() != media::StopReason::none)
            return media::unexpected{context.error()};

        media::Metadata facts;
        facts.track.title = "controlled title";
        facts.track.duration = duration;

        return facts;
    }

    media::DecoderOpenResult create_decoder(const media::OperationContext& context) override
    {
        if (context.stop_reason() != media::StopReason::none)
            return media::unexpected{context.error()};
        return std::unique_ptr<media::AudioDecoder>{std::make_unique<Decoder>()};
    }
};

class Provider final : public media::MediaProvider {
public:
    media::Result<std::unique_ptr<media::MediaAsset>> open(const media::ResourceReference&,
        const media::OperationContext& context) override
    {
        if (context.stop_reason() != media::StopReason::none)
            return media::unexpected{context.error()};
        return std::unique_ptr<media::MediaAsset>{std::make_unique<Asset>()};
    }

    std::vector<std::string> playback_extensions() const override { return {"controlled"}; }
};

class Processing final : public audio::Stage {
public:
    std::unique_ptr<Preparation> prepare_change(pcm::StreamSpec spec) noexcept override
    {
        configured = spec;
        return {};
    }

    void process(std::span<float> samples) noexcept override
    {
        for (float& sample : samples)
            sample *= 2.0F;
    }

    std::optional<pcm::StreamSpec> configured;
};

// Synchronous test output. Stop waits for any explicitly issued callback before returning.
class Device final : public audio::Output {
public:
    explicit Device(audio::Source& source) noexcept
        : source_{source}
    {
    }

    bool running() const noexcept override { return running_.load(); }

    pcm::Frames frames_played() const noexcept override { return pcm::Frames{frames_.load()}; }

    std::size_t consume(std::span<float> samples)
    {
        const std::lock_guard callback{callback_};

        if (!running())
            return 0;

        const auto count{source_.render(samples)};
        frames_.fetch_add(source_.spec().frames_per(count).count());

        return count;
    }

private:
    bool start() noexcept override
    {
        const std::lock_guard callback{callback_};

        frames_.store(0);
        running_.store(true);

        return true;
    }

    void stop() noexcept override
    {
        const std::lock_guard callback{callback_};
        running_.store(false);
    }

    audio::Source& source_;
    std::mutex callback_;
    std::atomic<bool> running_{};
    std::atomic<std::size_t> frames_{};
};

template<class Predicate>
bool await(engine::Session& session, Predicate settled)
{
    const auto deadline{std::chrono::steady_clock::now() + std::chrono::seconds{5}};

    do {
        session.catch_up();

        if (settled())
            return true;

        std::this_thread::yield();
    } while (std::chrono::steady_clock::now() < deadline);

    return settled();
}

int fail(std::string_view message)
{
    std::cerr << message << '\n';
    return 1;
}

} // namespace

int main()
{
    using namespace wiola;

    auto processing = core::Lease<Processing>::make();
    Device* device{};
    engine::Session session{processing,
        [&](audio::Source& source) {
            auto output{std::make_unique<Device>(source)};
            device = output.get();

            return output;
        },
        std::make_shared<Provider>()};
    const media::ResourceReference supplied{"memory", "opaque?track=17", "Controlled resource"};
    session.open({supplied});

    const auto request{session.open_request_id()};

    if (!await(session,
            [&] { return session.loaded() && session.playlist().metadata_revision() != 0; }))
        return fail("decoder/metadata did not settle through the supplied provider");

    const auto snapshot{session.snapshot()};
    const auto* current{session.playlist().current()};

    if (!current || snapshot.track != supplied || snapshot.track.label() != supplied.label() ||
        current->resource() != supplied || snapshot.open_result != engine::OpenResult::opened ||
        snapshot.total_time != duration || snapshot.queue_position != 0 ||
        processing->configured != stream || current->metadata().title != "controlled title")
        return fail("installed decoder facts or processing configuration were lost");

    const auto outcomes{session.take_outcomes()};

    if (std::ranges::none_of(outcomes.values, [&](const auto& value) {
            return value.operation == engine::OperationKind::open && value.id == request &&
                !value.error;
        }))
        return fail("accepted open did not retain its successful completion");

    if (!session.play() || !await(session, [&] { return device && device->running(); }))
        return fail("supplied output did not start");

    std::array<float, 16> samples{};

    if (!await(session, [&] { return device->consume(samples) != 0; }) ||
        !std::ranges::all_of(samples, [](float value) { return value == 0.5F; }))
        return fail("rendering did not pass through the supplied processing stage");

    if (!session.pause() || !await(session, [&] { return !device->running(); }) ||
        session.state() != engine::PlaybackState::paused)
        return fail("pause did not quiesce output");

    if (!session.resume() || !await(session, [&] { return device->running(); }))
        return fail("resume did not restart output");

    const units::Time position{std::chrono::milliseconds{250}};
    session.seek(position);

    if (!await(session, [&] { return session.time_played() >= position && device->running(); }))
        return fail("seek did not apply before output restart");

    session.stop();

    if (!await(session, [&] { return !device->running(); }) ||
        session.state() != engine::PlaybackState::stopped)
        return fail("stop did not settle transport/output");

    session.clear();

    if (!session.playlist().empty())
        return fail("clear retained queue occurrences");

    const auto resource{session.track()};
    const auto held{session.release_media(resource)};

    if (!held || session.loaded() || session.restore_media(*held) != engine::OpenResult::loading ||
        !await(session, [&] { return session.loaded(); }))
        return fail("retained media could not be handed off independently of its queue");

    if (!session.playlist().empty() || session.track() != resource || session.playing() ||
        session.time_played() != units::Time{})
        return fail("restoration changed queue selection, stopped intent or resource identity");

    return 0;
}
