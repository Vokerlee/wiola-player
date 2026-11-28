/**
 * @file
 * @brief Session commands, queue coordination and asynchronous loading contracts.
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

#include "fixture.hpp"

#include <wiola/audio/stage.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <memory>
#include <optional>
#include <random>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <vector>

namespace {

using wiola::core::Lease;
using wiola::engine::OpenResult;
using wiola::engine::Session;
using wiola::media::ResourceReference;
using wiola::pcm::Frames;
using wiola::testing::Gate;
using wiola::testing::session::poll_until;
using State = wiola::engine::PlaybackState;

const std::array catalog{
    wiola::testing::resource("alpha", "alpha"),
    wiola::testing::resource("beta", "beta"),
    wiola::testing::resource("gamma", "gamma"),
    wiola::testing::resource("unreadable", "unreadable"),
};
const auto held = wiola::testing::resource("held", "held");

wiola::pcm::StreamSpec shape(const ResourceReference& resource)
{
    return resource == catalog[1] ? wiola::pcm::StreamSpec{wiola::units::Frequency{48000}, 1}
                                  : wiola::pcm::StreamSpec{};
}

struct Probe {
    std::atomic<unsigned> decoders_created{}, decoders_destroyed{};
    std::atomic<unsigned> outputs_created{}, outputs_destroyed{};
    std::atomic<unsigned> unquiesced_destructions{}, cancelled_opens{};

    // Only the control thread reads/writes these setup fault switches.
    bool refuse_processing{};
    bool refuse_output{};
};

class TrackedDecoder final : public wiola::media::AudioDecoder {
public:
    TrackedDecoder(wiola::pcm::StreamSpec spec, std::shared_ptr<Probe> probe)
        : AudioDecoder{spec, spec.checked_frames_at(wiola::units::Time{20}).value()}
        , probe_{std::move(probe)}
    {
        ++probe_->decoders_created;
    }

    ~TrackedDecoder() override { ++probe_->decoders_destroyed; }

private:
    wiola::media::Result<wiola::media::DecodeOutcome> decode(std::span<float> output,
        Frames frames) override
    {
        std::fill_n(output.begin(), spec().samples_per(frames), 0.0F);
        return wiola::media::DecodeOutcome{.frames = frames};
    }

    wiola::media::Result<wiola::media::SeekOutcome> seek_frame(Frames target) override
    {
        return wiola::media::SeekOutcome{.target = target, .achieved = target};
    }

    std::shared_ptr<Probe> probe_;
};

// No consumer clock: reaching EOF cannot race the command model's state assertions.
class TrackedOutput final : public wiola::audio::Output {
public:
    explicit TrackedOutput(std::shared_ptr<Probe> probe)
        : probe_{std::move(probe)}
    {
        ++probe_->outputs_created;
    }

    ~TrackedOutput() override
    {
        if (running())
            ++probe_->unquiesced_destructions;

        ++probe_->outputs_destroyed;
    }

    bool running() const noexcept override { return running_.load(); }

    Frames frames_played() const noexcept override { return {}; }

private:
    bool start() noexcept override
    {
        running_.store(true);
        return true;
    }

    void stop() noexcept override { running_.store(false); }

    std::shared_ptr<Probe> probe_;
    std::atomic<bool> running_{};
};

class ControlledStage final : public wiola::audio::Stage {
public:
    explicit ControlledStage(std::shared_ptr<Probe> probe)
        : probe_{std::move(probe)}
    {
    }

    void process(std::span<float>) noexcept override { }

private:
    class Refused final : public Preparation {
        bool can_activate() const noexcept override { return false; }

        void activate() noexcept override { }
    };

    std::unique_ptr<Preparation> prepare_change(wiola::pcm::StreamSpec) override
    {
        if (probe_->refuse_processing)
            return std::make_unique<Refused>();

        return nullptr;
    }

    std::shared_ptr<Probe> probe_;
};

struct LifecycleRig {
    std::shared_ptr<Probe> probe = std::make_shared<Probe>();
    std::shared_ptr<Gate> opening = std::make_shared<Gate>();
    std::optional<Lease<ControlledStage>> stage = Lease<ControlledStage>::make(probe);
    std::weak_ptr<wiola::media::MediaProvider> provider_observer;
    std::unique_ptr<Session> session;

    LifecycleRig()
    {
        auto provider = wiola::testing::FakeMediaProvider::decoder_only(
            [probe = probe, opening = opening](const ResourceReference& resource,
                const wiola::media::OperationContext& context) -> wiola::media::DecoderOpenResult {
                if (resource == held) {
                    const std::stop_callback release{context.stop,
                        [opening] { opening->release(); }};
                    opening->wait();

                    if (context.stop.stop_requested())
                        ++probe->cancelled_opens;
                    // Deliberately return late success: the loader must reject its generation.
                }

                if (resource == catalog[3])
                    return wiola::media::unexpected{
                        wiola::media::Error{wiola::media::ErrorCategory::unreadable,
                                            "injected open failure"}
                    };

                return wiola::media::DecoderOpenResult{
                    std::make_unique<TrackedDecoder>(shape(resource), probe)};
            }
        );
        provider_observer = provider;
        session = std::make_unique<Session>(
            *stage,
            [probe = probe](wiola::audio::Source&) -> std::unique_ptr<wiola::audio::Output> {
                if (probe->refuse_output)
                    throw std::runtime_error{"injected output failure"};

                return std::make_unique<TrackedOutput>(probe);
            },
            std::move(provider)
        );
    }

    void shutdown()
    {
        const auto observer = stage->weak();
        stage.reset();
        EXPECT_TRUE(observer.lock());

        session.reset();

        EXPECT_FALSE(observer.lock());
        EXPECT_TRUE(provider_observer.expired());
        EXPECT_EQ(probe->decoders_created.load(), probe->decoders_destroyed.load());
        EXPECT_EQ(probe->outputs_created.load(), probe->outputs_destroyed.load());
        EXPECT_EQ(probe->unquiesced_destructions.load(), 0U);
    }
};

enum class Command {
    open,
    add,
    select,
    remove,
    clear,
    play,
    pause,
    resume,
    stop,
    seek,
    next,
    previous,
};

// Independent public-behavior model: values only, with no transport, loader or queue helper reuse.
struct Model {
    std::vector<ResourceReference> queue;
    std::optional<std::size_t> selected;
    ResourceReference installed;
    State state{State::idle};
    std::optional<OpenResult> load_result;

    void load(const ResourceReference& resource, bool play)
    {
        load_result = resource == catalog[3] ? OpenResult::unreadable : OpenResult::opened;

        if (*load_result == OpenResult::opened) {
            installed = resource;
            state = play ? State::playing : State::idle;
        }
    }

    std::optional<bool> apply(Command command, std::size_t value)
    {
        load_result.reset();

        switch (command) {
        case Command::open:
            queue = {catalog[value % catalog.size()], catalog[(value + 1) % catalog.size()]};
            selected = 0;
            load(queue.front(), false);
            break;
        case Command::add: {
            const bool empty = queue.empty();
            queue.push_back(catalog[value % catalog.size()]);

            if (empty) {
                selected = 0;
                if (installed.empty())
                    load(queue.front(), false);
            }
            break;
        }
        case Command::next:
        case Command::previous:
            if (!selected ||
                (command == Command::next ? *selected + 1 == queue.size() : *selected == 0))
                return false;

            value = command == Command::next ? *selected + 1 : *selected - 1;
            [[fallthrough]];
        case Command::select:
            if (value >= queue.size())
                return false;

            selected = value;
            load(queue[value], state == State::playing);
            return true;
        case Command::remove:
            if (value >= queue.size())
                return false;

            queue.erase(queue.begin() + static_cast<std::ptrdiff_t>(value));
            if (queue.empty())
                selected.reset();
            else if (*selected >= value)
                selected = *selected == value ? std::min(value, queue.size() - 1) : *selected - 1;
            return true;
        case Command::clear:
            queue.clear();
            selected.reset();
            [[fallthrough]];
        case Command::stop:
            if (!installed.empty())
                state = State::stopped;
            break;
        case Command::play:
            if (installed.empty() || state == State::playing || state == State::paused)
                return false;

            state = State::playing;
            return true;
        case Command::pause:
            if (state != State::playing)
                return false;

            state = State::paused;
            return true;
        case Command::resume:
            if (state != State::paused)
                return false;

            state = State::playing;
            return true;
        case Command::seek:
            break;
        }

        return std::nullopt;
    }
};

std::optional<bool> execute(Session& session, Command command, std::size_t value)
{
    switch (command) {
    case Command::open:
        session.open({catalog[value % catalog.size()], catalog[(value + 1) % catalog.size()]});
        break;
    case Command::add:
        session.add({catalog[value % catalog.size()]});
        break;
    case Command::select:
        return session.play_track(value);
    case Command::remove:
        return session.remove(value);
    case Command::clear:
        session.clear();
        break;
    case Command::play:
        return session.play();
    case Command::pause:
        return session.pause();
    case Command::resume:
        return session.resume();
    case Command::stop:
        session.stop();
        break;
    case Command::seek:
        session.seek(wiola::units::Time{static_cast<double>(value % 4)});
        break;
    case Command::next:
        return session.next_track();
    case Command::previous:
        return session.previous_track();
    }

    return std::nullopt;
}

bool settle(Session& session)
{
    if (!poll_until(session, [&] { return !session.reading(); }))
        return false;

    // A worker can publish between catch_up() and reading(). Consume that completion too.
    session.catch_up();
    return !session.reading();
}

void expect_model(const Session& session, const Model& model)
{
    ASSERT_EQ(session.playlist().tracks().size(), model.queue.size());
    EXPECT_EQ(session.playlist().position(), model.selected);

    for (std::size_t index = 0; index < model.queue.size(); ++index)
        EXPECT_EQ(session.playlist().tracks()[index].resource(), model.queue[index]);

    EXPECT_EQ(session.loaded(), !model.installed.empty());
    EXPECT_EQ(session.track(), model.installed);
    EXPECT_EQ(session.state(), model.state);
    EXPECT_EQ(session.playing(), model.state == State::playing);

    if (model.load_result)
        EXPECT_EQ(session.open_result(), *model.load_result);

    if (session.loaded()) {
        ASSERT_TRUE(session.total_time());
        EXPECT_EQ(*session.total_time(), wiola::units::Time{20});
        EXPECT_GE(session.time_played(), wiola::units::Time{});
        EXPECT_LE(session.time_played(), *session.total_time());
    }
}

TEST(SessionLifecycle, SeededCommandTracesMatchThePublicModelAndReleaseTheGraph)
{
    for (const unsigned seed : {7U, 31U, 419U, 65537U}) {
        SCOPED_TRACE(seed);
        LifecycleRig rig;
        Model model;
        std::mt19937 random{seed};

        for (unsigned step = 0; step < 96; ++step) {
            const auto command =
                static_cast<Command>(random() % (static_cast<unsigned>(Command::previous) + 1));
            const auto value = random() % (model.queue.size() + 4);
            SCOPED_TRACE(::testing::Message{} << "step=" << step << " command="
                                              << static_cast<int>(command) << " value=" << value);

            const auto expected = model.apply(command, value);
            EXPECT_EQ(execute(*rig.session, command, value), expected);
            ASSERT_TRUE(settle(*rig.session));
            expect_model(*rig.session, model);
        }

        rig.shutdown();
    }
}

TEST(SessionLifecycle, FailedPreparationsPreservePlayingMediaAndLaterRecoveryReplacesIt)
{
    LifecycleRig rig;
    Model model;
    model.queue = {catalog[0]};
    model.selected = 0;
    model.load(catalog[0], false);
    rig.session->open(model.queue);
    ASSERT_TRUE(settle(*rig.session));
    ASSERT_TRUE(rig.session->play());
    model.state = State::playing;

    for (const bool reject_processing : {true, false}) {
        rig.probe->refuse_processing = reject_processing;
        rig.probe->refuse_output = !reject_processing;
        model.queue = {catalog[1]};
        model.load_result = OpenResult::output_unavailable;
        rig.session->open(model.queue);

        ASSERT_TRUE(settle(*rig.session));
        expect_model(*rig.session, model);
    }

    rig.probe->refuse_processing = false;
    rig.probe->refuse_output = false;
    model.load(catalog[1], false);
    rig.session->open(model.queue);

    ASSERT_TRUE(settle(*rig.session));
    expect_model(*rig.session, model);
    rig.shutdown();
}

TEST(SessionLifecycle, SupersededBlockedSuccessCannotReplaceItsSuccessor)
{
    LifecycleRig rig;
    rig.session->open({held});
    rig.opening->wait_until_entered();
    rig.session->open({catalog[1]});

    ASSERT_TRUE(settle(*rig.session));
    EXPECT_EQ(rig.session->track(), catalog[1]);
    EXPECT_EQ(rig.session->open_result(), OpenResult::opened);
    EXPECT_EQ(rig.probe->cancelled_opens.load(), 1U);
    rig.shutdown();
}

TEST(SessionLifecycle, ShutdownCancelsBlockedWorkAndReleasesLateSuccessfulDecoders)
{
    LifecycleRig rig;
    rig.session->open({held});
    rig.opening->wait_until_entered();
    rig.shutdown();

    EXPECT_EQ(rig.probe->cancelled_opens.load(), 1U);
    EXPECT_EQ(rig.probe->decoders_created.load(), 1U);
}

} // namespace
