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

namespace wiola::app {

PlaybackService::PlaybackService(engine::Session& session)
    : session_{&session}
{
}

void PlaybackService::stop_accepting() noexcept
{
    accepting_ = false;
    session_->stop();
}

engine::OperationReceipt PlaybackService::open(std::vector<std::filesystem::path> tracks)
{
    if (!accepting_)
        return {.id = 0,
            .operation = engine::OperationKind::open,
            .acceptance = engine::CommandResult::unavailable};
    session_->open(std::move(tracks));

    return {.id = session_->open_request_id(),
        .operation = engine::OperationKind::open,
        .acceptance = engine::CommandResult::accepted};
}

void PlaybackService::add(std::vector<std::filesystem::path> tracks)
{
    if (!accepting_)
        return;
    session_->add(std::move(tracks));
}

bool PlaybackService::remove(std::size_t index)
{
    if (!accepting_)
        return false;
    return session_->remove(index);
}

void PlaybackService::clear()
{
    if (!accepting_)
        return;
    session_->clear();
}

bool PlaybackService::play_track(std::size_t index)
{
    if (!accepting_)
        return false;
    return session_->play_track(index);
}

bool PlaybackService::next_track()
{
    if (!accepting_)
        return false;
    return session_->next_track();
}

bool PlaybackService::previous_track()
{
    if (!accepting_)
        return false;
    return session_->previous_track();
}

engine::CommandResult PlaybackService::play() noexcept
{
    if (!accepting_)
        return engine::CommandResult::unavailable;
    return session_->play() ? engine::CommandResult::accepted : engine::CommandResult::unavailable;
}

engine::CommandResult PlaybackService::pause() noexcept
{
    if (!accepting_)
        return engine::CommandResult::unavailable;
    return session_->pause() ? engine::CommandResult::accepted : engine::CommandResult::unavailable;
}

engine::CommandResult PlaybackService::resume() noexcept
{
    if (!accepting_)
        return engine::CommandResult::unavailable;
    return session_->resume() ? engine::CommandResult::accepted
                              : engine::CommandResult::unavailable;
}

std::optional<engine::Session::MediaReplacementState>
PlaybackService::release_media(const std::filesystem::path& path)
{
    return session_->release_media(path);
}

void PlaybackService::restore_media(const engine::Session::MediaReplacementState& state)
{
    if (accepting_)
        static_cast<void>(session_->restore_media(state));
}

void PlaybackService::stop() noexcept
{
    session_->stop();
}

void PlaybackService::seek(units::Time position) noexcept
{
    if (!accepting_)
        return;
    session_->seek(position);
}

void PlaybackService::set_volume(float gain) noexcept
{
    if (!accepting_)
        return;
    session_->volume().set_gain(gain);
}

float PlaybackService::volume() const noexcept
{
    return session_->volume().gain();
}

void PlaybackService::set_equalizer_enabled(bool enabled) noexcept
{
    if (!accepting_)
        return;
    session_->equalizer().set_enabled(enabled);
}

bool PlaybackService::equalizer_enabled() const noexcept
{
    return session_->equalizer().enabled();
}

void PlaybackService::set_equalizer_band(std::size_t index, float db) noexcept
{
    if (!accepting_)
        return;
    session_->equalizer().set_band_gain(index, db);
}

float PlaybackService::equalizer_band(std::size_t index) const noexcept
{
    return session_->equalizer().band_gain(index);
}

std::size_t PlaybackService::num_equalizer_bands() const noexcept
{
    return session_->equalizer().num_bands();
}

const audio::VolumePolicy& PlaybackService::volume_policy() const noexcept
{
    return session_->volume().policy();
}

const audio::EqualizerConfig& PlaybackService::equalizer_config() const noexcept
{
    return session_->equalizer().config();
}

float PlaybackService::equalizer_preamp() const noexcept
{
    return session_->equalizer().preamp();
}

units::Frequency PlaybackService::equalizer_band_center(std::size_t index) const noexcept
{
    return session_->equalizer().band_center(index);
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
    const auto playback = snapshot();
    if (playback.loaded && playback.seekable && playback.total_time)
        seek(*playback.total_time * fraction);
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
    session_->catch_up();
    return session_->snapshot();
}

engine::OperationOutcomes PlaybackService::take_outcomes()
{
    return session_->take_outcomes();
}

engine::PlaybackSnapshot PlaybackService::snapshot() const
{
    return session_->snapshot();
}

bool PlaybackService::reading() const noexcept
{
    return session_->reading();
}

bool PlaybackService::loaded() const noexcept
{
    return session_->loaded();
}

bool PlaybackService::playing() const noexcept
{
    return session_->playing();
}

engine::OpenResult PlaybackService::open_result() const noexcept
{
    return session_->open_result();
}

engine::PlaybackState PlaybackService::state() const noexcept
{
    return session_->state();
}

units::Time PlaybackService::time_played() const noexcept
{
    return session_->time_played();
}

std::optional<units::Time> PlaybackService::total_time() const noexcept
{
    return session_->total_time();
}

bool PlaybackService::seekable() const noexcept
{
    return session_->seekable();
}

std::vector<std::string> PlaybackService::playback_extensions() const
{
    return session_->playback_extensions();
}

const engine::Playlist& PlaybackService::playlist() const noexcept
{
    return session_->playlist();
}

const std::filesystem::path& PlaybackService::track() const noexcept
{
    return session_->track();
}

void PlaybackService::set_repeat(engine::Playlist::Repeat repeat) noexcept
{
    if (!accepting_)
        return;
    session_->set_repeat(repeat);
}

engine::Playlist::Repeat PlaybackService::repeat() const noexcept
{
    return session_->repeat();
}

void PlaybackService::shuffle(bool on)
{
    if (!accepting_)
        return;
    session_->shuffle(on);
}

bool PlaybackService::shuffled() const noexcept
{
    return session_->shuffled();
}

} // namespace wiola::app
