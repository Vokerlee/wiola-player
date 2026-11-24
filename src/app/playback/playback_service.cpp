/**
 * @file
 * @brief Small application facade over playback, queue, loading, and audio values.
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

#include "playback_service.hpp"

#include <wiola/audio/equalizer.hpp>
#include <wiola/audio/processing_chain.hpp>
#include <wiola/audio/volume.hpp>

#include <memory>
#include <utility>

namespace wiola::app {

struct PlaybackService::Impl {
    Impl(engine::OutputFactory output, std::shared_ptr<media::MediaProvider> provider)
        : session{processing, std::move(output), std::move(provider)}
    {
    }

    Impl(engine::OutputFactory output, engine::DecoderOpener open_decoder,
        engine::MetadataReader read_metadata)
        : session{processing, std::move(output), std::move(open_decoder), std::move(read_metadata)}
    {
    }

    // Session/output teardown precedes destruction of the processing objects it borrows.
    audio::Equalizer equalizer{pcm::StreamSpec{}};
    audio::Volume volume;
    audio::ProcessingChain processing{
        {equalizer, volume}
    };
    engine::Session session;
    bool accepting{true};
};

PlaybackService::PlaybackService(engine::OutputFactory output,
    std::shared_ptr<media::MediaProvider> provider)
    : impl_{std::make_unique<Impl>(std::move(output), std::move(provider))}
{
}

PlaybackService::PlaybackService(engine::OutputFactory output, engine::DecoderOpener open_decoder,
    engine::MetadataReader read_metadata)
    : impl_{std::make_unique<Impl>(std::move(output), std::move(open_decoder),
          std::move(read_metadata))}
{
}

PlaybackService::~PlaybackService() = default;

void PlaybackService::stop_accepting() noexcept
{
    impl_->accepting = false;
    impl_->session.stop();
}

engine::OperationReceipt PlaybackService::open(std::vector<std::filesystem::path> tracks)
{
    if (!impl_->accepting)
        return {.id = 0,
            .operation = engine::OperationKind::open,
            .acceptance = engine::CommandResult::unavailable};
    impl_->session.open(std::move(tracks));

    return {.id = impl_->session.open_request_id(),
        .operation = engine::OperationKind::open,
        .acceptance = engine::CommandResult::accepted};
}

void PlaybackService::add(std::vector<std::filesystem::path> tracks)
{
    if (!impl_->accepting)
        return;
    impl_->session.add(std::move(tracks));
}

bool PlaybackService::remove(std::size_t index)
{
    if (!impl_->accepting)
        return false;
    return impl_->session.remove(index);
}

void PlaybackService::clear()
{
    if (!impl_->accepting)
        return;
    impl_->session.clear();
}

bool PlaybackService::play_track(std::size_t index)
{
    if (!impl_->accepting)
        return false;
    return impl_->session.play_track(index);
}

bool PlaybackService::next_track()
{
    if (!impl_->accepting)
        return false;
    return impl_->session.next_track();
}

bool PlaybackService::previous_track()
{
    if (!impl_->accepting)
        return false;
    return impl_->session.previous_track();
}

engine::CommandResult PlaybackService::play() noexcept
{
    if (!impl_->accepting)
        return engine::CommandResult::unavailable;
    return impl_->session.play() ? engine::CommandResult::accepted
                                 : engine::CommandResult::unavailable;
}

engine::CommandResult PlaybackService::pause() noexcept
{
    if (!impl_->accepting)
        return engine::CommandResult::unavailable;
    return impl_->session.pause() ? engine::CommandResult::accepted
                                  : engine::CommandResult::unavailable;
}

engine::CommandResult PlaybackService::resume() noexcept
{
    if (!impl_->accepting)
        return engine::CommandResult::unavailable;
    return impl_->session.resume() ? engine::CommandResult::accepted
                                   : engine::CommandResult::unavailable;
}

std::optional<engine::Session::MediaReplacementState>
PlaybackService::release_media(const std::filesystem::path& path)
{
    return impl_->session.release_media(path);
}

void PlaybackService::restore_media(const engine::Session::MediaReplacementState& state)
{
    if (impl_->accepting)
        static_cast<void>(impl_->session.restore_media(state));
}

void PlaybackService::stop() noexcept
{
    impl_->session.stop();
}

void PlaybackService::seek(units::Time position) noexcept
{
    if (!impl_->accepting)
        return;
    impl_->session.seek(position);
}

void PlaybackService::set_volume(float gain) noexcept
{
    if (!impl_->accepting)
        return;
    impl_->volume.set_gain(gain);
}

float PlaybackService::volume() const noexcept
{
    return impl_->volume.gain();
}

void PlaybackService::set_equalizer_enabled(bool enabled) noexcept
{
    if (!impl_->accepting)
        return;
    impl_->equalizer.set_enabled(enabled);
}

bool PlaybackService::equalizer_enabled() const noexcept
{
    return impl_->equalizer.enabled();
}

void PlaybackService::set_equalizer_band(std::size_t index, float db) noexcept
{
    if (!impl_->accepting)
        return;
    impl_->equalizer.set_band_gain(index, db);
}

float PlaybackService::equalizer_band(std::size_t index) const noexcept
{
    return impl_->equalizer.band_gain(index);
}

std::size_t PlaybackService::num_equalizer_bands() const noexcept
{
    return impl_->equalizer.num_bands();
}

const audio::VolumePolicy& PlaybackService::volume_policy() const noexcept
{
    return impl_->volume.policy();
}

const audio::EqualizerConfig& PlaybackService::equalizer_config() const noexcept
{
    return impl_->equalizer.config();
}

float PlaybackService::equalizer_preamp() const noexcept
{
    return impl_->equalizer.preamp();
}

units::Frequency PlaybackService::equalizer_band_center(std::size_t index) const noexcept
{
    return impl_->equalizer.band_center(index);
}

engine::CommandResult PlaybackService::toggle_playback() noexcept
{
    switch (state()) {
    case engine::PlaybackState::playing:
        return pause();
    case engine::PlaybackState::paused:
        return resume();
    default:
        return play();
    }
}

void PlaybackService::seek_to(double fraction) noexcept
{
    if (const auto duration{total_time()})
        seek(*duration * fraction);
}

void PlaybackService::cycle_repeat() noexcept
{
    using Repeat = engine::Playlist::Repeat;
    switch (repeat()) {
    case Repeat::none:
        set_repeat(Repeat::all);
        break;
    case Repeat::all:
        set_repeat(Repeat::track);
        break;
    case Repeat::track:
        set_repeat(Repeat::none);
        break;
    }
}

PlayerPresentation present_playback(const PlaybackService& service)
{
    return {.playback = service.snapshot(),
        .repeat = service.repeat(),
        .shuffled = service.shuffled()};
}

engine::PlaybackSnapshot PlaybackService::pump()
{
    impl_->session.catch_up();
    return impl_->session.snapshot();
}

engine::OperationOutcomes PlaybackService::take_outcomes()
{
    return impl_->session.take_outcomes();
}

engine::PlaybackSnapshot PlaybackService::snapshot() const
{
    return impl_->session.snapshot();
}

bool PlaybackService::reading() const noexcept
{
    return impl_->session.reading();
}

bool PlaybackService::loaded() const noexcept
{
    return impl_->session.loaded();
}

bool PlaybackService::playing() const noexcept
{
    return impl_->session.playing();
}

engine::OpenResult PlaybackService::open_result() const noexcept
{
    return impl_->session.open_result();
}

engine::PlaybackState PlaybackService::state() const noexcept
{
    return impl_->session.state();
}

units::Time PlaybackService::time_played() const noexcept
{
    return impl_->session.time_played();
}

std::optional<units::Time> PlaybackService::total_time() const noexcept
{
    return impl_->session.total_time();
}

bool PlaybackService::seekable() const noexcept
{
    return impl_->session.seekable();
}

std::vector<std::string> PlaybackService::playback_extensions() const
{
    return impl_->session.playback_extensions();
}

const engine::Playlist& PlaybackService::playlist() const noexcept
{
    return impl_->session.playlist();
}

const std::filesystem::path& PlaybackService::track() const noexcept
{
    return impl_->session.track();
}

void PlaybackService::set_repeat(engine::Playlist::Repeat repeat) noexcept
{
    if (!impl_->accepting)
        return;
    impl_->session.set_repeat(repeat);
}

engine::Playlist::Repeat PlaybackService::repeat() const noexcept
{
    return impl_->session.repeat();
}

void PlaybackService::shuffle(bool on)
{
    if (!impl_->accepting)
        return;
    impl_->session.shuffle(on);
}

bool PlaybackService::shuffled() const noexcept
{
    return impl_->session.shuffled();
}

} // namespace wiola::app
