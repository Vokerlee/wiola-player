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

#include "playlist.hpp"

#include <algorithm>
#include <type_traits>
#include <utility>

namespace wiola::engine {

namespace {

media::TrackMetadata with_fallback_title(const std::filesystem::path& path,
    media::TrackMetadata metadata)
{
    if (metadata.title.empty())
        metadata.title = path.stem().string();
    return metadata;
}

std::pair<std::vector<QueueEntry>, QueueEntryId>
make_entries(std::vector<std::filesystem::path> paths, QueueEntryId next)
{
    std::vector<QueueEntry> entries;
    entries.reserve(paths.size());
    for (auto& path : paths) {
        auto metadata{with_fallback_title(path, {})};
        entries.emplace_back(next, std::move(path), std::move(metadata));
        ++next.value;
    }
    return {std::move(entries), next};
}

} // namespace

void Playlist::set(std::vector<std::filesystem::path> paths)
{
    auto [tracks, next]{make_entries(std::move(paths), next_id_)};
    std::map<QueueEntryId, std::size_t> indices;

    for (std::size_t index{0}; index < tracks.size(); ++index) {
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

void Playlist::add(std::vector<std::filesystem::path> paths)
{
    if (paths.empty())
        return;

    auto [tracks, next]{make_entries(std::move(paths), next_id_)};
    tracks_.reserve(tracks_.size() + tracks.size());
    order_.reserve(tracks_.size() + tracks.size());

    std::map<QueueEntryId, std::size_t> additions;

    for (const QueueEntry& track : tracks) {
        additions.emplace(track.id(), tracks_.size() + additions.size());
    }

    indices_.merge(additions);

    for (QueueEntry& track : tracks) {
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
    const QueueEntryId previous_id{current()->id()};
    indices_.erase(tracks_[index].id());
    tracks_.erase(tracks_.begin() + static_cast<std::ptrdiff_t>(index));
    static_cast<void>(order_.remove(index));

    for (auto& [id, position] : indices_) {
        if (position > index)
            --position;
    }

    ++revision_;
    ++structural_revision_;
    const QueueEntry* selected{current()};
    const QueueEntryId selected_id{selected ? selected->id() : QueueEntryId{}};
    if (previous_position != position() || previous_id != selected_id)
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

std::size_t Playlist::revision() const noexcept
{
    return revision_;
}

std::span<const QueueEntry> Playlist::tracks() const noexcept
{
    return tracks_;
}

const QueueEntry* Playlist::current() const noexcept
{
    const auto selected{order_.position()};
    return selected ? &tracks_[*selected] : nullptr;
}

bool Playlist::describe(QueueEntryId id, media::TrackMetadata metadata)
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

void Playlist::apply_metadata(std::span<const QueueEntryId> entries,
    const media::TrackMetadata& metadata, std::shared_ptr<const media::Artwork> artwork)
{
    std::vector<std::pair<std::size_t, media::TrackMetadata>> descriptions;
    for (const QueueEntryId id : entries) {
        const auto index{index_of(id)};
        if (!index)
            continue;

        descriptions.emplace_back(*index, with_fallback_title(tracks_[*index].path_, metadata));
    }

    // All allocation completes before changing any occurrence or revision.
    static_assert(std::is_nothrow_move_assignable_v<media::TrackMetadata>,
        "Prepared queue metadata must commit without allocation");
    for (auto& [index, description] : descriptions) {
        ++tracks_[index].revision_;
        tracks_[index].metadata_ = std::move(description);
        tracks_[index].artwork_ = artwork;
    }
    if (descriptions.empty())
        return;
    ++revision_;
    ++metadata_revision_;
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

void Playlist::set_repeat(RepeatMode repeat) noexcept
{
    order_.set_repeat(repeat);
}

RepeatMode Playlist::repeat() const noexcept
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
