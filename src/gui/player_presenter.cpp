/**
 * @file
 * @brief Persistent commands and presentation state for player widgets.
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

#include "player_presenter.hpp"

#include <utility>

namespace wiola::gui {

PlayerPresenter::PlayerPresenter() = default;

PlayerPresenter::PlayerPresenter(engine::Session& session)
    : service_{session}
{
}

PlayerPresenter::PlayerPresenter(engine::OutputFactory make_output)
    : service_{std::move(make_output)}
{
}

PlayerPresenter::PlayerPresenter(engine::OutputFactory make_output,
    engine::DecoderOpener open_decoder, engine::MetadataReader read_metadata)
    : service_{std::move(make_output), std::move(open_decoder), std::move(read_metadata)}
{
}

engine::OperationReceipt PlayerPresenter::open(std::vector<std::filesystem::path> tracks)
{
    return service_.open(std::move(tracks));
}

void PlayerPresenter::add(std::vector<std::filesystem::path> tracks)
{
    service_.add(std::move(tracks));
}

bool PlayerPresenter::remove(std::size_t index)
{
    return service_.remove(index);
}

void PlayerPresenter::clear()
{
    service_.clear();
}

bool PlayerPresenter::play_track(std::size_t index)
{
    return service_.play_track(index);
}

bool PlayerPresenter::next_track()
{
    return service_.next_track();
}

bool PlayerPresenter::previous_track()
{
    return service_.previous_track();
}

engine::CommandResult PlayerPresenter::toggle_playback() noexcept
{
    switch (service_.state()) {
    case engine::Playback::State::playing:
        return service_.pause();

    case engine::Playback::State::paused:
        return service_.resume();

    default:
        return service_.play();
    }
}

void PlayerPresenter::stop() noexcept
{
    service_.stop();
}

void PlayerPresenter::seek_to(double fraction) noexcept
{
    const engine::PlaybackSnapshot playback{service_.snapshot()};

    if (playback.loaded && playback.seekable && playback.total_time)
        service_.seek(*playback.total_time * fraction);
}

void PlayerPresenter::set_volume(float gain) noexcept
{
    service_.set_volume(gain);
}

float PlayerPresenter::volume() const noexcept
{
    return service_.volume();
}

void PlayerPresenter::set_equalizer_enabled(bool enabled) noexcept
{
    service_.set_equalizer_enabled(enabled);
}

bool PlayerPresenter::equalizer_enabled() const noexcept
{
    return service_.equalizer_enabled();
}

void PlayerPresenter::set_equalizer_band(std::size_t index, float db) noexcept
{
    service_.set_equalizer_band(index, db);
}

float PlayerPresenter::equalizer_band(std::size_t index) const noexcept
{
    return service_.equalizer_band(index);
}

std::size_t PlayerPresenter::num_equalizer_bands() const noexcept
{
    return service_.num_equalizer_bands();
}

audio::Volume& PlayerPresenter::volume_control() noexcept
{
    return service_.volume_control();
}

audio::Equalizer& PlayerPresenter::equalizer_control() noexcept
{
    return service_.equalizer_control();
}

void PlayerPresenter::set_repeat(engine::Playlist::Repeat repeat) noexcept
{
    service_.set_repeat(repeat);
}

void PlayerPresenter::cycle_repeat() noexcept
{
    using Repeat = engine::Playlist::Repeat;

    switch (service_.repeat()) {
    case Repeat::none:
        service_.set_repeat(Repeat::all);
        break;

    case Repeat::all:
        service_.set_repeat(Repeat::track);
        break;

    case Repeat::track:
        service_.set_repeat(Repeat::none);
        break;
    }
}

void PlayerPresenter::shuffle(bool on)
{
    service_.shuffle(on);
}

const engine::Playlist& PlayerPresenter::playlist() const noexcept
{
    return service_.playlist();
}

std::vector<std::string> PlayerPresenter::playback_extensions() const
{
    return service_.playback_extensions();
}

PlayerPresentation PlayerPresenter::pump()
{
    return {.playback = service_.pump(),
        .repeat = service_.repeat(),
        .shuffled = service_.shuffled()};
}

PlayerPresentation PlayerPresenter::presentation() const
{
    return {.playback = service_.snapshot(),
        .repeat = service_.repeat(),
        .shuffled = service_.shuffled()};
}

bool PlayerPresenter::reading() const noexcept
{
    return service_.reading();
}

} // namespace wiola::gui
