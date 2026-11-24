/**
 * @file
 * @brief Plays one decoder through a device.
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

#include "player.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

namespace wiola::engine {

Player::Player(std::unique_ptr<media::AudioDecoder> source, pcm::PcmQueue::Producer buffer,
    audio::Output& output, TransportConfig config)
    : output_{output}
    , spec_{source->spec()}
    , length_{source->duration_frames()}
    , length_accuracy_{source->length_accuracy()}
    , seekable_{source->seekable()}
    , worker_{std::move(source), std::move(buffer), output, {}, config}
{
}

Player::~Player() = default;

bool Player::start()
{
    return worker_.start();
}

void Player::seek(units::Time position) noexcept
{
    if (!seekable_ || !std::isfinite(position.seconds()))
        return;

    const std::optional<pcm::Frames> target{
        spec_.checked_frames_at(std::max(position, units::Time{}))};

    const bool outside_exact_source{target && length_accuracy_ == media::LengthAccuracy::exact &&
        length_ && *target > *length_};
    if (target && !outside_exact_source)
        worker_.seek(*target);
}

bool Player::pause() noexcept
{
    return worker_.pause();
}

bool Player::resume() noexcept
{
    return worker_.resume();
}

std::optional<units::Time> Player::total_time() const noexcept
{
    return length_ ? std::optional{spec_.time_per(*length_)} : std::nullopt;
}

units::Time Player::time_played() const noexcept
{
    return spec_.time_per(worker_.position(output_.frames_played()));
}

bool Player::playing() const noexcept
{
    return worker_.playing();
}

void Player::stop() noexcept
{
    worker_.stop();
}

PlaybackState Player::state() const noexcept
{
    return worker_.state();
}

bool Player::finished() const noexcept
{
    return worker_.finished();
}

void Player::wait()
{
    worker_.wait();
}

} // namespace wiola::engine
