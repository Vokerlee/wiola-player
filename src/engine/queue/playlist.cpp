/**
 * @file
 * @brief Queue entry storage, metadata, and one coherent current selection.
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
#include <utility>

namespace wiola::engine {

void Playlist::set(std::vector<Track> tracks)
{
    ++revision_;
    tracks_ = std::move(tracks);

    for (Track& track : tracks_)
        identify(track);

    order_.reset(tracks_.size());
}

void Playlist::add(Track track)
{
    ++revision_;
    identify(track);
    tracks_.push_back(std::move(track));
    order_.add();
}

bool Playlist::remove(std::size_t index)
{
    if (!order_.remove(index))
        return false;

    ++revision_;
    tracks_.erase(tracks_.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

void Playlist::clear() noexcept
{
    ++revision_;
    tracks_.clear();
    order_.clear();
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
    const auto selected{order_.position()};
    return selected ? &tracks_[*selected] : nullptr;
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
    return order_.position();
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
    return order_.go_to(index);
}

bool Playlist::next() noexcept
{
    return order_.next();
}

bool Playlist::previous() noexcept
{
    return order_.previous();
}

void Playlist::set_repeat(Repeat repeat) noexcept
{
    order_.set_repeat(repeat);
}

Playlist::Repeat Playlist::repeat() const noexcept
{
    return order_.repeat();
}

void Playlist::shuffle(std::uint_fast32_t seed)
{
    ++revision_;
    order_.shuffle(seed);
}

void Playlist::unshuffle() noexcept
{
    ++revision_;
    order_.unshuffle();
}

bool Playlist::shuffled() const noexcept
{
    return order_.shuffled();
}

void Playlist::identify(Track& track)
{
    track.id_ = next_id_;
    ++next_id_.value;
}

} // namespace wiola::engine
