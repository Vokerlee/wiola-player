/**
 * @file
 * @brief Queue entry storage, indexed metadata, and one coherent current selection.
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

#include <engine/queue/playlist.hpp>

#include <algorithm>
#include <utility>

namespace wiola::engine {

void Playlist::set(std::vector<Track> tracks)
{
    ++revision_;
    ++structural_revision_;
    tracks_ = std::move(tracks);

    for (Track& track : tracks_)
        identify(track);

    reindex();
    order_.reset(tracks_.size());
}

void Playlist::add(Track track)
{
    ++revision_;
    ++structural_revision_;
    identify(track);
    indices_.insert_or_assign(track.id(), tracks_.size());
    tracks_.push_back(std::move(track));
    order_.add();
}

bool Playlist::remove(std::size_t index)
{
    const auto previous_position{position()};
    if (!order_.remove(index))
        return false;

    ++revision_;
    ++structural_revision_;
    indices_.erase(tracks_[index].id());
    tracks_.erase(tracks_.begin() + static_cast<std::ptrdiff_t>(index));
    reindex();
    if (previous_position != position())
        ++selection_revision_;
    return true;
}

void Playlist::clear() noexcept
{
    ++revision_;
    ++structural_revision_;
    if (position())
        ++selection_revision_;
    tracks_.clear();
    indices_.clear();
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
    ++metadata_revision_;
    tracks_[*index].metadata_ = std::move(metadata);
    return true;
}

bool Playlist::illustrate(QueueEntryId id, std::shared_ptr<const media::Artwork> artwork)
{
    const auto index{index_of(id)};

    if (!index)
        return false;

    ++revision_;
    ++metadata_revision_;
    tracks_[*index].artwork_ = std::move(artwork);
    return true;
}

void Playlist::apply_metadata(std::span<const QueueEntryId> entries, const TrackMetadata& metadata,
    std::shared_ptr<const media::Artwork> artwork)
{
    bool changed{false};

    for (const QueueEntryId id : entries) {
        const auto index{index_of(id)};

        if (!index)
            continue;

        tracks_[*index].metadata_ = metadata;
        tracks_[*index].artwork_ = artwork;
        changed = true;
    }

    if (changed) {
        ++revision_;
        ++metadata_revision_;
    }
}

std::optional<std::size_t> Playlist::position() const noexcept
{
    return order_.position();
}

std::optional<std::size_t> Playlist::index_of(QueueEntryId id) const noexcept
{
    const auto found{indices_.find(id)};
    return found == indices_.end() ? std::nullopt : std::optional{found->second};
}

bool Playlist::go_to(std::size_t index) noexcept
{
    const bool changed{order_.go_to(index)};
    if (changed)
        ++selection_revision_;
    return changed;
}

bool Playlist::next() noexcept
{
    const bool changed{order_.next()};
    if (changed)
        ++selection_revision_;
    return changed;
}

bool Playlist::previous() noexcept
{
    const bool changed{order_.previous()};
    if (changed)
        ++selection_revision_;
    return changed;
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
    ++structural_revision_;
    order_.shuffle(seed);
}

void Playlist::unshuffle() noexcept
{
    ++revision_;
    ++structural_revision_;
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

void Playlist::reindex() noexcept
{
    indices_.clear();
    for (std::size_t index = 0; index < tracks_.size(); ++index)
        indices_.insert_or_assign(tracks_[index].id(), index);
}

} // namespace wiola::engine
