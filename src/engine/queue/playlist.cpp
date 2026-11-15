/**
 * @file
 * @brief What is to be played, and where in it playback has reached.
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

#include <engine/queue/playlist.hpp>

#include <algorithm>
#include <numeric>
#include <random>
#include <utility>

namespace wiola::engine {

void Playlist::set(std::vector<Track> tracks)
{
    ++revision_;
    tracks_ = std::move(tracks);

    for (Track& track : tracks_)
        identify(track);

    order_.resize(tracks_.size());
    std::iota(order_.begin(), order_.end(), std::size_t{0});
    shuffled_ = false;
    place_ = tracks_.empty() ? std::nullopt : std::optional<std::size_t>{0};
}

void Playlist::add(Track track)
{
    ++revision_;
    identify(track);
    tracks_.push_back(std::move(track));

    // A track added to a shuffled list goes at the end of the order rather than into the middle.
    order_.push_back(tracks_.size() - 1);

    if (!place_.has_value())
        place_ = 0;
}

bool Playlist::remove(std::size_t index)
{
    if (index >= tracks_.size())
        return false;

    ++revision_;
    const std::optional<std::size_t> standing{position()};
    tracks_.erase(tracks_.begin() + static_cast<std::ptrdiff_t>(index));
    order_.erase(std::ranges::find(order_, index));

    for (std::size_t& place : order_)
        if (place > index)
            --place;

    if (tracks_.empty()) {
        place_.reset();
        return true;
    }

    if (!standing.has_value()) {
        place_ = 0;
        return true;
    }

    if (*standing == index) {
        place_ = place_of(std::min(index, tracks_.size() - 1));
        return true;
    }

    place_ = place_of(*standing > index ? *standing - 1 : *standing);
    return true;
}

void Playlist::clear() noexcept
{
    ++revision_;
    tracks_.clear();
    order_.clear();
    place_.reset();
    shuffled_ = false;
}

bool Playlist::empty() const noexcept
{
    return tracks_.empty();
}

std::size_t Playlist::revision() const noexcept
{
    return revision_;
}

const std::vector<Track>& Playlist::tracks() const noexcept
{
    return tracks_;
}

const Track* Playlist::current() const noexcept
{
    return place_.has_value() ? &tracks_[order_[*place_]] : nullptr;
}

bool Playlist::describe(QueueEntryId id, TrackMetadata metadata)
{
    const auto index{index_of(id)};

    if (!index)
        return false;

    ++revision_;
    tracks_[*index].metadata_ = std::move(metadata);
    return true;
}

std::optional<std::size_t> Playlist::position() const noexcept
{
    if (!place_.has_value())
        return std::nullopt;

    return order_[*place_];
}

std::optional<std::size_t> Playlist::index_of(QueueEntryId id) const noexcept
{
    const auto found = std::ranges::find(tracks_, id, &Track::id);

    if (found == tracks_.end())
        return std::nullopt;

    return static_cast<std::size_t>(std::distance(tracks_.begin(), found));
}

bool Playlist::go_to(std::size_t index) noexcept
{
    if (index >= tracks_.size())
        return false;

    place_ = place_of(index);
    return true;
}

bool Playlist::next() noexcept
{
    if (!place_.has_value())
        return false;

    if (repeat_ == Repeat::track)
        return true;

    if (*place_ + 1 < order_.size()) {
        place_ = *place_ + 1;
        return true;
    }

    if (repeat_ != Repeat::all)
        return false;

    place_ = 0;
    return true;
}

bool Playlist::previous() noexcept
{
    if (!place_.has_value())
        return false;

    if (repeat_ == Repeat::track)
        return true;

    if (*place_ > 0) {
        place_ = *place_ - 1;
        return true;
    }

    if (repeat_ != Repeat::all)
        return false;

    place_ = order_.size() - 1;
    return true;
}

void Playlist::set_repeat(Repeat repeat) noexcept
{
    repeat_ = repeat;
}

Playlist::Repeat Playlist::repeat() const noexcept
{
    return repeat_;
}

void Playlist::shuffle(std::uint_fast32_t seed)
{
    ++revision_;
    const std::optional<std::size_t> standing{position()};
    std::mt19937 random{seed};
    std::ranges::shuffle(order_, random);
    shuffled_ = true;

    if (standing.has_value())
        place_ = place_of(*standing);
}

void Playlist::unshuffle()
{
    ++revision_;
    const std::optional<std::size_t> standing{position()};
    std::iota(order_.begin(), order_.end(), std::size_t{0});
    shuffled_ = false;

    if (standing.has_value())
        place_ = place_of(*standing);
}

bool Playlist::shuffled() const noexcept
{
    return shuffled_;
}

std::size_t Playlist::place_of(std::size_t index) const noexcept
{
    const auto found = std::ranges::find(order_, index);
    return static_cast<std::size_t>(std::distance(order_.begin(), found));
}

void Playlist::identify(Track& track)
{
    track.id_ = next_id_;
    ++next_id_.value;
}

} // namespace wiola::engine
