/**
 * @file
 * @brief One track, everything it plays through, and what the listener asked of it.
 * @author Roman Glaz
 * @copyright © 2026, <vokerlee@gmail.com>
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

#include <engine/session.hpp>

#include <engine/load/loader.hpp>
#include <engine/load/tag_reader.hpp>
#include <engine/transport/player.hpp>

#include <audio/device/device.hpp>
#include <audio/dsp/buffer_source.hpp>
#include <audio/dsp/relay.hpp>
#include <audio/dsp/shaped_source.hpp>
#include <codec/decode/decoder.hpp>
#include <codec/decode/open.hpp>
#include <pcm/pcm_queue.hpp>
#include <pcm/stream_spec.hpp>

#include <memory>
#include <optional>
#include <random>
#include <stdexcept>
#include <vector>

#include <utility>

namespace wiola::engine {

Session::Session()
    : Session{[](audio::Source& source) { return std::make_unique<audio::Device>(source); }}
{
}

Session::Session(OutputFactory make_output)
    : Session{std::move(make_output), codec::open_file, codec::read_tags}
{
}

Session::Session(OutputFactory make_output, DecoderOpener open_decoder,
    MetadataReader read_metadata)
    : Session{std::move(make_output), std::move(open_decoder), std::move(read_metadata), {}, {}}
{
}

Session::Session(OutputFactory make_output, DecoderOpener open_decoder,
    MetadataReader read_metadata, TransportConfig transport, BufferingConfig buffering)
    // In the order they are declared, which is the order they are built in whatever this says.
    : chain_{
          {equalizer_, volume_}
},
    loader_{std::make_unique<Loader>(std::move(open_decoder))},
    tags_{std::make_unique<TagReader>(std::move(read_metadata))},
    out_{chain_, std::move(make_output)}, transport_{transport}, buffering_{buffering}
{
    if (!transport_.valid() || !buffering_.valid())
        throw std::invalid_argument{"invalid engine configuration"};
}

Session::~Session() = default;

codec::OpenResult Session::open(std::vector<std::filesystem::path> tracks)
{
    std::vector<Track> queued;
    queued.reserve(tracks.size());

    for (const std::filesystem::path& track : tracks)
        queued.push_back(Track::of(track));

    playlist_.set(std::move(queued));

    begin_reading_tags(playlist_.tracks());

    if (playlist_.empty())
        return last_result_;

    return begin_reading(playlist_.current()->path(), Waiting::install);
}

bool Session::next_track()
{
    const bool was_playing{playing()};

    if (!playlist_.next())
        return false;

    static_cast<void>(read_current_track(was_playing));

    return true;
}

bool Session::previous_track()
{
    const bool was_playing{playing()};

    if (!playlist_.previous())
        return false;

    static_cast<void>(read_current_track(was_playing));

    return true;
}

void Session::add(std::vector<std::filesystem::path> tracks)
{
    const bool was_empty{playlist_.empty()};
    const std::size_t first_new{playlist_.tracks().size()};

    for (const std::filesystem::path& track : tracks)
        playlist_.add(Track::of(track));

    begin_reading_tags(std::span<const Track>{playlist_.tracks()}.subspan(first_new));

    // Adding to an empty queue is the first thing a listener does, and they mean to play it.
    if (was_empty && !playlist_.empty() && !loaded())
        static_cast<void>(begin_reading(playlist_.current()->path(), Waiting::install));
}

bool Session::remove(std::size_t index)
{
    return playlist_.remove(index);
}

void Session::clear()
{
    playlist_.clear();
    stop();
}

const Playlist& Session::playlist() const noexcept
{
    return playlist_;
}

bool Session::play_track(std::size_t index)
{
    const bool was_playing{playing()};

    if (!playlist_.go_to(index))
        return false;

    static_cast<void>(read_current_track(was_playing));

    return true;
}

void Session::set_repeat(Playlist::Repeat repeat) noexcept
{
    playlist_.set_repeat(repeat);
}

Playlist::Repeat Session::repeat() const noexcept
{
    return playlist_.repeat();
}

void Session::shuffle(bool on)
{
    if (!on) {
        playlist_.unshuffle();

        return;
    }

    std::random_device seeds;

    playlist_.shuffle(seeds());
}

bool Session::shuffled() const noexcept
{
    return playlist_.shuffled();
}

codec::OpenResult Session::begin_reading(const std::filesystem::path& path, Waiting waiting)
{
    // Whatever was read and not put on was read for a track nobody is waiting for any more.
    ready_.reset();
    ready_track_.clear();

    loader_->start(path);
    opening_ = path;
    waiting_ = waiting;
    last_result_ = codec::OpenResult::loading;

    return last_result_;
}

codec::OpenResult Session::read_current_track(bool playing)
{
    return begin_reading(playlist_.current()->path(), playing ? Waiting::play : Waiting::install);
}

void Session::catch_up()
{
    take_up_finished_read();
    take_up_finished_tags();
    advance_if_ended();
}

void Session::begin_reading_tags(std::span<const Track> tracks)
{
    std::vector<TagReader::Request> requests;
    requests.reserve(tracks.size());

    for (const Track& track : tracks)
        requests.push_back(TagReader::Request{.id = track.id(), .path = track.path()});

    tags_->start(std::move(requests));
}

void Session::take_up_finished_tags()
{
    for (const TagReader::Reading& reading : tags_->take()) {
        TrackMetadata metadata{
            .title = reading.tags.title.empty() ? reading.path.stem().string() : reading.tags.title,
            .artist = reading.tags.artist,
            .album = reading.tags.album,
            .duration = reading.tags.duration,
        };

        static_cast<void>(playlist_.describe(reading.id, std::move(metadata)));
    }
}

void Session::take_up_finished_read()
{
    std::optional<codec::Opened> opened{loader_->take()};

    if (!opened.has_value())
        return;

    if (!*opened) {
        // Nothing is torn down: a file that would not open is no reason to silence the one that
        // did.
        last_result_ = opened->result;

        return;
    }

    ready_ = std::move(opened->decoder);
    ready_track_ = opening_;
    last_result_ = codec::OpenResult::opened;

    const bool play{waiting_ == Waiting::play};

    if (install_read_track() && play)
        static_cast<void>(play_or_pause());
}

void Session::advance_if_ended()
{
    // A track that ran out is the cue to play the next. A listener who pressed stop is not, and
    // neither is a device that went away.
    if (state() != Playback::State::ended)
        return;

    // One already on its way is the same cue, answered.
    if (reading() || read_waiting())
        return;

    if (!playlist_.next())
        return;

    static_cast<void>(read_current_track(true));
}

bool Session::read_waiting() const noexcept
{
    return ready_ != nullptr;
}

bool Session::install_read_track()
{
    if (!read_waiting())
        return false;

    std::unique_ptr<codec::Decoder> source{std::move(ready_)};
    const pcm::StreamSpec spec{source->spec()};
    const std::optional<pcm::Frames> buffer_frames{spec.checked_frames_at(buffering_.duration)};

    if (!buffer_frames || !pcm::PcmQueue::checked_capacity(spec, *buffer_frames)) {
        ready_track_.clear();
        last_result_ = codec::OpenResult::damaged;
        return false;
    }

    // Disconnect first. Resetting the pipeline then stops its player and waits for its output
    // connection to quiesce before the old buffer source or decoder can be destroyed.
    out_.disconnect();
    pipeline_.reset();

    chain_.configure(spec);
    out_.open(spec);

    auto pipeline{std::make_unique<PlaybackPipeline>(std::move(source), out_.output(),
        *buffer_frames, transport_)};
    pipeline_ = std::move(pipeline);
    out_.connect(pipeline_->decoded());

    track_ = ready_track_;
    ready_track_.clear();

    return true;
}

bool Session::reading() const noexcept
{
    return loader_->busy();
}

codec::OpenResult Session::open_result() const noexcept
{
    return last_result_;
}

const std::filesystem::path& Session::track() const noexcept
{
    return track_;
}

bool Session::loaded() const noexcept
{
    return pipeline_ != nullptr;
}

bool Session::play_or_pause()
{
    if (!loaded())
        return false;

    // The state says which of the three this means: silence it, carry on, or begin - the last of
    // which also covers playing a track that has already finished.
    switch (state()) {
    case Playback::State::playing:
        return pipeline_->player().pause();
    case Playback::State::paused:
        return pipeline_->player().resume();
    default:
        return pipeline_->player().start();
    }
}

void Session::stop() noexcept
{
    if (!loaded())
        return;

    pipeline_->player().stop();

    // Stopping is not pausing: the next play begins at the start of the track.
    pipeline_->player().seek(units::Time{});
}

void Session::seek(units::Time position) noexcept
{
    if (loaded())
        pipeline_->player().seek(position);
}

Playback::State Session::state() const noexcept
{
    return loaded() ? pipeline_->player().state() : Playback::State::idle;
}

bool Session::playing() const noexcept
{
    return loaded() && pipeline_->player().playing();
}

units::Time Session::time_played() const noexcept
{
    return loaded() ? pipeline_->player().time_played() : units::Time{};
}

units::Time Session::total_time() const noexcept
{
    return loaded() ? pipeline_->player().total_time() : units::Time{};
}

} // namespace wiola::engine
