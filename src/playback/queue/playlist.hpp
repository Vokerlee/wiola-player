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
#include <wiola/playback/queue_view.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <vector>

namespace wiola::engine {

/// Owns queue entries while PlaybackOrder owns permutation, cursor, and repeat decisions.
class Playlist final : public QueueView {
public:
    void set(std::vector<std::filesystem::path> paths);
    void add(std::vector<std::filesystem::path> paths);
    bool remove(std::size_t index) noexcept;
    void clear() noexcept;

    [[nodiscard]] std::size_t revision() const noexcept;

    [[nodiscard]] std::size_t structural_revision() const noexcept override
    {
        return structural_revision_;
    }

    [[nodiscard]] std::size_t metadata_revision() const noexcept override
    {
        return metadata_revision_;
    }

    [[nodiscard]] std::size_t selection_revision() const noexcept { return selection_revision_; }

    [[nodiscard]] std::span<const QueueEntry> tracks() const noexcept override;
    [[nodiscard]] const QueueEntry* current() const noexcept override;

    bool describe(QueueEntryId id, media::TrackMetadata metadata);
    bool illustrate(QueueEntryId id, std::shared_ptr<const media::Artwork> artwork);
    void apply_metadata(std::span<const QueueEntryId> entries, const media::TrackMetadata& metadata,
        std::shared_ptr<const media::Artwork> artwork);

    [[nodiscard]] std::optional<std::size_t> position() const noexcept override;
    [[nodiscard]] std::optional<std::size_t> index_of(QueueEntryId id) const noexcept override;

    bool go_to(std::size_t index) noexcept;
    bool next() noexcept;
    bool previous() noexcept;

    void set_repeat(RepeatMode repeat) noexcept;
    [[nodiscard]] RepeatMode repeat() const noexcept;

    void shuffle(std::uint_fast32_t seed);
    void unshuffle() noexcept;
    [[nodiscard]] bool shuffled() const noexcept;

private:
    std::vector<QueueEntry> tracks_;
    std::map<QueueEntryId, std::size_t> indices_;
    PlaybackOrder order_;
    std::size_t revision_{0};
    std::size_t structural_revision_{0};
    std::size_t metadata_revision_{0};
    std::size_t selection_revision_{0};
    QueueEntryId next_id_{1};
};

} // namespace wiola::engine
