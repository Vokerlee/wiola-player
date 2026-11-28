/**
 * @file
 * @brief One track, everything it plays through, and what the listener asked of it.
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

#include "session_impl.hpp"

#include <buffering/pcm_queue.hpp>
#include <loading/loader.hpp>
#include <transport/player.hpp>
#include <wiola/audio/stage.hpp>
#include <wiola/media/audio_decoder.hpp>
#include <wiola/media/stream_spec.hpp>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <memory>
#include <optional>
#include <random>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace wiola::engine {

namespace {

std::shared_ptr<media::MediaProvider> validated(std::shared_ptr<media::MediaProvider> provider,
    const TransportConfig& transport, const BufferingConfig& buffering)
{
    if (!provider)
        throw std::invalid_argument{"playback requires a media provider"};

    if (!transport.valid() || !buffering.valid())
        throw std::invalid_argument{"invalid playback configuration"};

    return provider;
}

} // namespace

Session::Impl::Impl(core::Lease<audio::Stage> processing, OutputFactory make_output,
    std::shared_ptr<media::MediaProvider> provider, TransportConfig transport,
    BufferingConfig buffering)
    : processing_{processing}
    , provider_{validated(std::move(provider), transport, buffering)}
    , loader_{provider_}
    , metadata_{provider_}
    , out_{processing_, std::move(make_output)}
    , transport_{transport}
    , buffering_{buffering}
{
}

OpenResult Session::Impl::open(std::vector<media::ResourceReference> tracks)
{
    open_request_id_ = 0;

    playlist_.set(std::move(tracks));
    replacement_.reset();

    metadata_.replace(playlist_.tracks());

    if (playlist_.empty()) {
        loads_.invalidate();
        loader_.cancel();

        return last_result_;
    }

    return begin_reading(playlist_.current()->id(), playlist_.current()->resource(),
        LoadIntent::install);
}

bool Session::Impl::next_track()
{
    const bool was_playing{wants_playback()};

    if (!playlist_.next())
        return false;

    static_cast<void>(read_current_track(was_playing));

    return true;
}

bool Session::Impl::previous_track()
{
    const bool was_playing{wants_playback()};

    if (!playlist_.previous())
        return false;

    static_cast<void>(read_current_track(was_playing));

    return true;
}

void Session::Impl::add(std::vector<media::ResourceReference> tracks)
{
    const bool was_empty{playlist_.empty()};
    const std::size_t first_new{playlist_.tracks().size()};

    playlist_.add(std::move(tracks));

    metadata_.append(playlist_.tracks().subspan(first_new));

    // Adding to an empty queue is the first thing a listener does, and they mean to play it.
    if (was_empty && !playlist_.empty() && !loaded())
        static_cast<void>(begin_reading(playlist_.current()->id(), playlist_.current()->resource(),
            LoadIntent::install));
}

bool Session::Impl::remove(std::size_t index)
{
    if (index >= playlist_.tracks().size())
        return false;

    const QueueEntryId entry{playlist_.tracks()[index].id()};

    if (loads_.invalidate_if(entry))
        loader_.cancel();

    if (replacement_ && entry == replacement_->intent.entry) {
        replacement_.reset();
    }

    return playlist_.remove(index);
}

void Session::Impl::clear()
{
    metadata_.cancel();
    playlist_.clear();
    replacement_.reset();
    stop();
}

const QueueView& Session::Impl::playlist() const noexcept
{
    return playlist_;
}

bool Session::Impl::play_track(std::size_t index)
{
    const bool was_playing{wants_playback()};

    if (!playlist_.go_to(index))
        return false;

    static_cast<void>(read_current_track(was_playing));

    return true;
}

void Session::Impl::set_repeat(RepeatMode repeat) noexcept
{
    playlist_.set_repeat(repeat);
}

RepeatMode Session::Impl::repeat() const noexcept
{
    return playlist_.repeat();
}

void Session::Impl::shuffle(bool on)
{
    if (!on) {
        playlist_.unshuffle();

        return;
    }

    std::random_device seeds;

    playlist_.shuffle(seeds());
}

bool Session::Impl::shuffled() const noexcept
{
    return playlist_.shuffled();
}

OpenResult Session::Impl::begin_reading(QueueEntryId entry, media::ResourceReference resource,
    LoadIntent intent, std::optional<units::Time> position)
{
    replacement_.reset();

    LoadRequest request{loads_.begin(entry, std::move(resource), intent, position)};
    open_request_id_ = request.generation;
    loader_.start(std::move(request));
    last_result_ = OpenResult::loading;

    return last_result_;
}

bool Session::Impl::wants_playback() const noexcept
{
    return replacement_ ? replacement_->intent.play : playing();
}

void Session::Impl::restore_when_ready()
{
    if (!replacement_ || !replacement_->restore_requested || loads_.pending())
        return;

    const auto intent{replacement_->intent};
    // Own the path argument before begin_reading retires the held intent. The installed
    // occurrence can have left the queue; restoration does not change its selection.
    static_cast<void>(begin_reading(intent.entry, track_,
        intent.play ? LoadIntent::play : LoadIntent::install, intent.position));
}

void Session::Impl::record_outcome(OperationKind operation, OperationId id,
    std::optional<OperationError> error)
{
    constexpr std::size_t max_retained_outcomes{64};

    // Appending can allocate; retain the accepted history if it fails.
    outcomes_.push_back(
        OperationOutcome{.id = id, .operation = operation, .error = std::move(error)}
    );

    if (outcomes_.size() > max_retained_outcomes) {
        outcomes_.pop_front();
        outcomes_overflowed_ = true;
    }
}

OpenResult Session::Impl::read_current_track(bool playing)
{
    // Called only after next/previous/go_to accepted a selection on this control thread.
    const QueueEntry& current{*playlist_.current()};
    return begin_reading(current.id(), current.resource(),
        playing ? LoadIntent::play : LoadIntent::install);
}

void Session::Impl::catch_up()
{
    take_up_finished_read();
    restore_when_ready();
    take_up_finished_tags();
    advance_if_ended();
}

void Session::Impl::take_up_finished_tags()
{
    for (MetadataCoordinator::Reading& reading : metadata_.take()) {
        std::optional<OperationError> error;

        if (!reading.result)
            error = reading.result.error();

        record_outcome(OperationKind::metadata, reading.entries.front().value, std::move(error));

        media::Metadata tags =
            reading.result ? std::move(reading.result).value() : media::Metadata{};
        auto artwork{std::make_shared<const media::Artwork>(std::move(tags.artwork))};
        playlist_.apply_metadata(reading.entries, tags.track, std::move(artwork));
    }
}

void Session::Impl::take_up_finished_read()
{
    std::optional<LoadCompletion> completed{loader_.take()};

    if (!completed || !loads_.accept(*completed))
        return;

    media::DecoderOpenResult& opened{completed->opened};

    if (!opened) {
        record_outcome(OperationKind::open, completed->request.generation, opened.error());

        // Nothing is torn down: a file that would not open is no reason to silence the one that
        // did.
        last_result_ = open_failed(opened.error().category);

        return;
    }

    const bool should_play{completed->request.intent == LoadIntent::play};

    if (!install_track(std::move(*opened), std::move(completed->request.resource),
            completed->request.entry, completed->request.generation)) {
        return;
    }

    if (completed->request.position)
        seek(*completed->request.position);

    if (should_play)
        static_cast<void>(play());
}

void Session::Impl::advance_if_ended()
{
    // A track that ran out is the cue to play the next. A listener who pressed stop is not, and
    // neither is a device that went away.
    if (state() != PlaybackState::ended)
        return;

    // One already on its way is the same cue, answered.
    if (reading())
        return;

    if (!playlist_.next())
        return;

    static_cast<void>(read_current_track(true));
}

bool Session::Impl::install_track(std::unique_ptr<media::AudioDecoder> source,
    media::ResourceReference resource, QueueEntryId entry, OperationId request)
{
    const auto reject{[this, request](OperationError error) {
        const auto status{open_failed(error)};

        record_outcome(OperationKind::open, request, std::move(error));
        last_result_ = status;

        return false;
    }};

    if (!source)
        return reject(media::Error{media::ErrorCategory::damaged,
            "decoder opener returned no source"});

    if (const auto valid{source->stream_info().validate()}; !valid)
        return reject(valid.error());

    const pcm::StreamSpec spec{source->spec()};
    const std::optional<pcm::Frames> buffer_frames{spec.checked_frames_at(buffering_.duration)};

    if (!buffer_frames || *buffer_frames == pcm::Frames{} ||
        !pcm::PcmQueue::checked_capacity(spec, *buffer_frames))
        return reject(media::Error{media::ErrorCategory::damaged,
            "decoder stream cannot support the configured playback buffer"});

    try {
        auto processing{audio::Stage::prepare(processing_, spec)};
        if (processing && !processing->ready())
            return reject(OutputSetupError{"processing preparation is no longer current"});

        auto output{out_.prepare(spec)};
        auto pipeline{std::make_unique<PlaybackPipeline>(std::move(source), std::move(output),
            *buffer_frames, transport_)};

        // This is the last fallible operation. One lifecycle caller observes the completion
        // after this function returns; the following commit cannot fail or allocate.
        record_outcome(OperationKind::open, request);
        auto previous =
            pipeline_ ? pipeline_->quiesce_output() : std::optional<audio::Output::Quiescence>{};
        pipeline_.reset();

        if (processing)
            processing->commit();

        const bool activated = pipeline->activate(out_, std::move(previous));
        assert(activated && "validated playback candidate must accept its stopped output");
        static_cast<void>(activated);
        pipeline_.swap(pipeline);
        static_assert(std::is_nothrow_move_assignable_v<media::ResourceReference>);
        track_ = std::move(resource);
        installed_entry_ = entry;
        replacement_.reset();
        last_result_ = OpenResult::opened;

        return true;
    } catch (const OutputSession::Unavailable& failure) {
        return reject(OutputSetupError{failure.what()});
    }
}

bool Session::Impl::reading() const noexcept
{
    return loader_.busy();
}

OpenResult Session::Impl::open_result() const noexcept
{
    return last_result_;
}

const media::ResourceReference& Session::Impl::track() const noexcept
{
    return track_;
}

bool Session::Impl::loaded() const noexcept
{
    return pipeline_ != nullptr;
}

bool Session::Impl::play() noexcept
{
    if (replacement_) {
        replacement_->intent.play = true;
        return true;
    }
    return loaded() && pipeline_->player().start();
}

bool Session::Impl::pause() noexcept
{
    if (replacement_) {
        replacement_->intent.play = false;
        return true;
    }
    return loaded() && pipeline_->player().pause();
}

bool Session::Impl::resume() noexcept
{
    if (replacement_) {
        replacement_->intent.play = true;
        return true;
    }
    return loaded() && pipeline_->player().resume();
}

void Session::Impl::stop() noexcept
{
    if (replacement_) {
        replacement_->intent.play = false;
        replacement_->intent.position = units::Time{};
    }

    loads_.invalidate();
    loader_.cancel();

    if (!loaded())
        return;

    pipeline_->player().stop();

    // Stopping is not pausing: the next play begins at the start of the track.
    pipeline_->player().seek(units::Time{});
}

std::optional<Session::MediaReplacementState>
Session::Impl::release_media(const media::ResourceReference& resource)
{
    if (resource != track_)
        return {};

    if (replacement_)
        return replacement_->intent;

    if (!loaded())
        return {};

    MediaReplacementState state{installed_entry_, playing(), time_played(),
        ++replacement_generation_};

    if (const auto pending{loads_.invalidate_resource(resource)}) {
        // A newer occurrence of the same physical resource must close its pending open too.
        // Preserve that occurrence's new-track intent instead of the old decoder's position.
        state.entry = pending->entry;
        state.play = pending->intent == LoadIntent::play;
        state.position = pending->position.value_or(units::Time{});
        loader_.cancel();
    }

    pipeline_.reset();
    replacement_ = Handoff{.intent = state};

    return state;
}

OpenResult Session::Impl::restore_media(const MediaReplacementState& state)
{
    if (!replacement_ || replacement_->intent.entry != state.entry ||
        replacement_->intent.generation != state.generation)
        return last_result_;

    replacement_->restore_requested = true;
    restore_when_ready();

    return last_result_;
}

void Session::Impl::seek(units::Time position) noexcept
{
    if (replacement_) {
        // Match the active player's command admission before storing request identity.
        // In particular, NaN would not even compare equal to its own copied completion.
        if (std::isfinite(position.seconds()))
            replacement_->intent.position = std::max(position, units::Time{});
        return;
    }
    if (loaded())
        pipeline_->player().seek(position);
}

PlaybackState Session::Impl::state() const noexcept
{
    return loaded() ? pipeline_->player().state() : PlaybackState::idle;
}

bool Session::Impl::playing() const noexcept
{
    return loaded() && pipeline_->player().playing();
}

units::Time Session::Impl::time_played() const noexcept
{
    return loaded() ? pipeline_->player().time_played() : units::Time{};
}

std::optional<units::Time> Session::Impl::total_time() const noexcept
{
    return loaded() ? pipeline_->player().total_time() : std::nullopt;
}

bool Session::Impl::seekable() const noexcept
{
    return loaded() && pipeline_->player().seekable();
}

std::vector<std::string> Session::Impl::playback_extensions() const
{
    return provider_->playback_extensions();
}

PlaybackSnapshot Session::Impl::snapshot() const
{
    return {.state = state(),
        .open_result = open_result(),
        .track = track(),
        .queue_position = playlist_.position(),
        .structural_revision = playlist_.structural_revision(),
        .metadata_revision = playlist_.metadata_revision(),
        .selection_revision = playlist_.selection_revision(),
        .time_played = time_played(),
        .total_time = total_time(),
        .loaded = loaded(),
        .playing = playing(),
        .seekable = seekable()};
}

OperationId Session::Impl::open_request_id() const noexcept
{
    return open_request_id_;
}

OperationOutcomes Session::Impl::take_outcomes()
{
    OperationOutcomes taken;
    taken.values.reserve(outcomes_.size());

    for (OperationOutcome& outcome : outcomes_)
        taken.values.push_back(std::move(outcome));

    outcomes_.clear();
    taken.overflowed = std::exchange(outcomes_overflowed_, false);

    return taken;
}

} // namespace wiola::engine
