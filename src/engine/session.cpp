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
#include <engine/session.hpp>

#include <memory>
#include <utility>

namespace wiola::engine {

Session::Session(audio::Stage& processing, OutputFactory make_output,
    std::shared_ptr<media::MediaProvider> provider, TransportConfig transport,
    BufferingConfig buffering)
    : impl_{std::make_unique<Impl>(processing, std::move(make_output), std::move(provider),
          transport, buffering)}
{
}

Session::~Session() = default;

OpenResult Session::open(std::vector<std::filesystem::path> tracks)
{
    return impl_->open(std::move(tracks));
}

bool Session::next_track()
{
    return impl_->next_track();
}

bool Session::previous_track()
{
    return impl_->previous_track();
}

void Session::add(std::vector<std::filesystem::path> tracks)
{
    impl_->add(std::move(tracks));
}

bool Session::remove(std::size_t index)
{
    return impl_->remove(index);
}

void Session::clear()
{
    impl_->clear();
}

const QueueView& Session::playlist() const noexcept
{
    return impl_->playlist();
}

bool Session::play_track(std::size_t index)
{
    return impl_->play_track(index);
}

void Session::set_repeat(RepeatMode repeat) noexcept
{
    impl_->set_repeat(repeat);
}

RepeatMode Session::repeat() const noexcept
{
    return impl_->repeat();
}

void Session::shuffle(bool on)
{
    impl_->shuffle(on);
}

bool Session::shuffled() const noexcept
{
    return impl_->shuffled();
}

void Session::catch_up()
{
    impl_->catch_up();
}

bool Session::reading() const noexcept
{
    return impl_->reading();
}

OpenResult Session::open_result() const noexcept
{
    return impl_->open_result();
}

const std::filesystem::path& Session::track() const noexcept
{
    return impl_->track();
}

bool Session::loaded() const noexcept
{
    return impl_->loaded();
}

bool Session::play() noexcept
{
    return impl_->play();
}

bool Session::pause() noexcept
{
    return impl_->pause();
}

bool Session::resume() noexcept
{
    return impl_->resume();
}

void Session::stop() noexcept
{
    impl_->stop();
}

std::optional<Session::MediaReplacementState>
Session::release_media(const std::filesystem::path& path)
{
    return impl_->release_media(path);
}

OpenResult Session::restore_media(const MediaReplacementState& state)
{
    return impl_->restore_media(state);
}

void Session::seek(units::Time position) noexcept
{
    impl_->seek(position);
}

PlaybackState Session::state() const noexcept
{
    return impl_->state();
}

bool Session::playing() const noexcept
{
    return impl_->playing();
}

units::Time Session::time_played() const noexcept
{
    return impl_->time_played();
}

std::optional<units::Time> Session::total_time() const noexcept
{
    return impl_->total_time();
}

bool Session::seekable() const noexcept
{
    return impl_->seekable();
}

std::vector<std::string> Session::playback_extensions() const
{
    return impl_->playback_extensions();
}

PlaybackSnapshot Session::snapshot() const
{
    return impl_->snapshot();
}

OperationId Session::open_request_id() const noexcept
{
    return impl_->open_request_id();
}

OperationOutcomes Session::take_outcomes()
{
    return impl_->take_outcomes();
}

} // namespace wiola::engine
