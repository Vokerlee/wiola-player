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

#include <queue/playlist.hpp>

#include <algorithm>
#include <utility>

namespace wiola::engine {

void Playlist::set(std::vector<Track> tracks)
{
    QueueEntryId next{next_id_};
    std::map<QueueEntryId, std::size_t> indices;

    for (std::size_t index{0}; index < tracks.size(); ++index) {
        tracks[index].id_ = next;
        ++next.value;
        indices.emplace(tracks[index].id(), index);
    }

    PlaybackOrder order{order_};
    order.reset(tracks.size());

    tracks_ = std::move(tracks);
    indices_.swap(indices);
    order_ = std::move(order);
    next_id_ = next;
    ++revision_;
    ++structural_revision_;
}

void Playlist::add(Track track)
{
    add(std::vector<Track>{std::move(track)});
}

void Playlist::add(std::vector<Track> tracks)
{
    if (tracks.empty())
        return;

    tracks_.reserve(tracks_.size() + tracks.size());
    order_.reserve(tracks_.size() + tracks.size());

    QueueEntryId next{next_id_};
    std::map<QueueEntryId, std::size_t> additions;

    for (Track& track : tracks) {
        track.id_ = next;
        ++next.value;
        additions.emplace(track.id(), tracks_.size() + additions.size());
    }

    indices_.merge(additions);

    for (Track& track : tracks) {
        tracks_.push_back(std::move(track));
        order_.add();
    }

    next_id_ = next;
    ++revision_;
    ++structural_revision_;
}

bool Playlist::remove(std::size_t index) noexcept
{
    if (index >= tracks_.size())
        return false;

    const auto previous_position{position()};
    indices_.erase(tracks_[index].id());
    tracks_.erase(tracks_.begin() + static_cast<std::ptrdiff_t>(index));
    static_cast<void>(order_.remove(index));

    for (auto& [id, position] : indices_) {
        if (position > index)
            --position;
    }

    ++revision_;
    ++structural_revision_;
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
    ++tracks_[*index].revision_;
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
    ++tracks_[*index].revision_;
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

        ++tracks_[*index].revision_;
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

} // namespace wiola::engine
