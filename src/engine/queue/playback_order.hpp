/**
 * @file
 * @brief Playback permutation, cursor, and repeat policy.
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

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace wiola::engine {

/// Owns playback-order decisions over stable entry indices without owning the entries themselves.
class PlaybackOrder {
public:
    enum class Repeat {
        none,
        track,
        all,
    };

    void reset(std::size_t size);
    void add();
    [[nodiscard]] bool remove(std::size_t index) noexcept;
    void clear() noexcept;

    /// Current entry index in insertion order, or nothing when no entry is selected.
    [[nodiscard]] std::optional<std::size_t> position() const noexcept;
    [[nodiscard]] bool go_to(std::size_t index) noexcept;
    [[nodiscard]] bool next() noexcept;
    [[nodiscard]] bool previous() noexcept;

    void set_repeat(Repeat repeat) noexcept { repeat_ = repeat; }

    [[nodiscard]] Repeat repeat() const noexcept { return repeat_; }

    void shuffle(std::uint_fast32_t seed);
    void unshuffle() noexcept;

    [[nodiscard]] bool shuffled() const noexcept { return shuffled_; }

private:
    [[nodiscard]] std::size_t place_of(std::size_t index) const noexcept;

    std::vector<std::size_t> order_;
    std::optional<std::size_t> place_;
    Repeat repeat_{Repeat::none};
    bool shuffled_{false};
};

} // namespace wiola::engine
