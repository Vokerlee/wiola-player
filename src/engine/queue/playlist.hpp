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

#pragma once

#include <engine/queue/track.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace wiola::engine {

/// Queue entries in insertion order, a stable playback order, and an optional current selection.
class Playlist {
public:
    enum class Repeat {
        none,
        track,
        all,
    };

    /// Replaces everything and selects the first entry. New identities are always assigned.
    void set(std::vector<Track> tracks);

    /// Appends an entry with a fresh identity; an empty list selects it.
    void add(Track track);

    bool remove(std::size_t index);
    void clear() noexcept;

    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::size_t revision() const noexcept;
    [[nodiscard]] const std::vector<Track>& tracks() const noexcept;

    /// The selected entry, or nullptr when there is no selection.
    [[nodiscard]] const Track* current() const noexcept;

    /// Updates only metadata for `id`, leaving identity and location untouched.
    bool describe(QueueEntryId id, TrackMetadata metadata);

    [[nodiscard]] std::optional<std::size_t> position() const noexcept;
    [[nodiscard]] std::optional<std::size_t> index_of(QueueEntryId id) const noexcept;

    bool go_to(std::size_t index) noexcept;
    bool next() noexcept;
    bool previous() noexcept;

    void set_repeat(Repeat repeat) noexcept;
    [[nodiscard]] Repeat repeat() const noexcept;

    /// Draws a deterministic playback order while preserving the selected entry.
    void shuffle(std::uint_fast32_t seed);
    void unshuffle();
    [[nodiscard]] bool shuffled() const noexcept;

private:
    [[nodiscard]] std::size_t place_of(std::size_t index) const noexcept;
    void identify(Track& track);

    std::vector<Track> tracks_;
    std::vector<std::size_t> order_;
    std::optional<std::size_t> place_;
    std::size_t revision_{0};
    QueueEntryId next_id_{1};
    Repeat repeat_{Repeat::none};
    bool shuffled_{false};
};

} // namespace wiola::engine
