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

#include <app/playback_service.hpp>

namespace wiola::engine {

PlaybackService::PlaybackService() = default;

PlaybackService::PlaybackService(OutputFactory make_output)
    : session_{std::move(make_output)}
{
}

PlaybackService::PlaybackService(OutputFactory make_output,
    std::shared_ptr<media::MediaProvider> provider)
    : session_{std::move(make_output), std::move(provider)}
{
}

PlaybackService::PlaybackService(OutputFactory make_output, DecoderOpener open_decoder,
    MetadataReader read_metadata)
    : session_{std::move(make_output), std::move(open_decoder), std::move(read_metadata)}
{
}

OperationReceipt PlaybackService::open(std::vector<std::filesystem::path> tracks)
{
    session_.open(std::move(tracks));

    return {.id = session_.open_request_id(),
        .operation = OperationKind::open,
        .acceptance = CommandResult::accepted};
}

void PlaybackService::add(std::vector<std::filesystem::path> tracks)
{
    session_.add(std::move(tracks));
}

bool PlaybackService::remove(std::size_t index)
{
    return session_.remove(index);
}

void PlaybackService::clear()
{
    session_.clear();
}

bool PlaybackService::play_track(std::size_t index)
{
    return session_.play_track(index);
}

bool PlaybackService::next_track()
{
    return session_.next_track();
}

bool PlaybackService::previous_track()
{
    return session_.previous_track();
}

CommandResult PlaybackService::play() noexcept
{
    return session_.play() ? CommandResult::accepted : CommandResult::unavailable;
}

CommandResult PlaybackService::pause() noexcept
{
    return session_.pause() ? CommandResult::accepted : CommandResult::unavailable;
}

CommandResult PlaybackService::resume() noexcept
{
    return session_.resume() ? CommandResult::accepted : CommandResult::unavailable;
}

void PlaybackService::stop() noexcept
{
    session_.stop();
}

void PlaybackService::seek(units::Time position) noexcept
{
    session_.seek(position);
}

void PlaybackService::set_volume(float gain) noexcept
{
    session_.volume().set_gain(gain);
}

float PlaybackService::volume() const noexcept
{
    return session_.volume().gain();
}

void PlaybackService::set_equalizer_enabled(bool enabled) noexcept
{
    session_.equalizer().set_enabled(enabled);
}

bool PlaybackService::equalizer_enabled() const noexcept
{
    return session_.equalizer().enabled();
}

void PlaybackService::set_equalizer_band(std::size_t index, float db) noexcept
{
    session_.equalizer().set_band_gain(index, db);
}

float PlaybackService::equalizer_band(std::size_t index) const noexcept
{
    return session_.equalizer().band_gain(index);
}

std::size_t PlaybackService::num_equalizer_bands() const noexcept
{
    return session_.equalizer().num_bands();
}

PlaybackSnapshot PlaybackService::pump()
{
    session_.catch_up();
    return session_.snapshot();
}

OperationOutcomes PlaybackService::take_outcomes()
{
    return session_.take_outcomes();
}

PlaybackSnapshot PlaybackService::snapshot() const
{
    return session_.snapshot();
}

void PlaybackService::catch_up()
{
    session_.catch_up();
}

bool PlaybackService::reading() const noexcept
{
    return session_.reading();
}

bool PlaybackService::loaded() const noexcept
{
    return session_.loaded();
}

bool PlaybackService::playing() const noexcept
{
    return session_.playing();
}

OpenResult PlaybackService::open_result() const noexcept
{
    return session_.open_result();
}

Playback::State PlaybackService::state() const noexcept
{
    return session_.state();
}

units::Time PlaybackService::time_played() const noexcept
{
    return session_.time_played();
}

std::optional<units::Time> PlaybackService::total_time() const noexcept
{
    return session_.total_time();
}

bool PlaybackService::seekable() const noexcept
{
    return session_.seekable();
}

std::vector<std::string> PlaybackService::playback_extensions() const
{
    return session_.playback_extensions();
}

const Playlist& PlaybackService::playlist() const noexcept
{
    return session_.playlist();
}

const std::filesystem::path& PlaybackService::track() const noexcept
{
    return session_.track();
}

void PlaybackService::set_repeat(Playlist::Repeat repeat) noexcept
{
    session_.set_repeat(repeat);
}

Playlist::Repeat PlaybackService::repeat() const noexcept
{
    return session_.repeat();
}

void PlaybackService::shuffle(bool on)
{
    session_.shuffle(on);
}

bool PlaybackService::shuffled() const noexcept
{
    return session_.shuffled();
}

} // namespace wiola::engine
