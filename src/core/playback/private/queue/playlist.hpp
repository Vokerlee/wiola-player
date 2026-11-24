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

#pragma once

#include <queue/playback_order.hpp>
#include <queue/track.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <vector>

namespace wiola::engine {

/// Owns queue entries while PlaybackOrder owns permutation, cursor, and repeat decisions.
class Playlist {
public:
    using Repeat = PlaybackOrder::Repeat;

    void set(std::vector<Track> tracks);
    void add(Track track);
    void add(std::vector<Track> tracks);
    bool remove(std::size_t index) noexcept;
    void clear() noexcept;

    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::size_t revision() const noexcept;

    [[nodiscard]] std::size_t structural_revision() const noexcept { return structural_revision_; }

    [[nodiscard]] std::size_t metadata_revision() const noexcept { return metadata_revision_; }

    [[nodiscard]] std::size_t selection_revision() const noexcept { return selection_revision_; }

    [[nodiscard]] const std::vector<Track>& tracks() const noexcept;
    [[nodiscard]] const Track* current() const noexcept;

    bool describe(QueueEntryId id, TrackMetadata metadata);
    bool illustrate(QueueEntryId id, std::shared_ptr<const media::Artwork> artwork);
    void apply_metadata(std::span<const QueueEntryId> entries, const TrackMetadata& metadata,
        std::shared_ptr<const media::Artwork> artwork);

    [[nodiscard]] std::optional<std::size_t> position() const noexcept;
    [[nodiscard]] std::optional<std::size_t> index_of(QueueEntryId id) const noexcept;

    bool go_to(std::size_t index) noexcept;
    bool next() noexcept;
    bool previous() noexcept;

    void set_repeat(Repeat repeat) noexcept;
    [[nodiscard]] Repeat repeat() const noexcept;

    void shuffle(std::uint_fast32_t seed);
    void unshuffle() noexcept;
    [[nodiscard]] bool shuffled() const noexcept;

private:
    std::vector<Track> tracks_;
    std::map<QueueEntryId, std::size_t> indices_;
    PlaybackOrder order_;
    std::size_t revision_{0};
    std::size_t structural_revision_{0};
    std::size_t metadata_revision_{0};
    std::size_t selection_revision_{0};
    QueueEntryId next_id_{1};
};

} // namespace wiola::engine
