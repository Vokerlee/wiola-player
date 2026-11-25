/**
 * @file
 * @brief Bounded application-owned encoded artwork and negative results.
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
#include <list>
#include <map>
#include <wiola/library/identity.hpp>
#include <wiola/media/artwork_reference.hpp>

namespace wiola::app {

/// Owner-thread encoded artwork store with explicit byte/item limits and deterministic LRU.
class ArtworkStore final {
public:
    explicit ArtworkStore(std::size_t bytes = 64 * 1024 * 1024, std::size_t entries = 4096);
    bool put(const library::MediaId& media, std::shared_ptr<const media::Artwork> artwork);
    bool fail(const library::MediaId& media);
    void remove(const library::MediaId& media);
    [[nodiscard]] media::ArtworkReference get(const library::MediaId& media);

    [[nodiscard]] std::size_t bytes() const { return bytes_; }

    [[nodiscard]] std::size_t size() const { return entries_.size(); }

private:
    struct Entry {
        std::shared_ptr<const media::Artwork> bytes;
        media::ArtworkReference reference;
        std::list<library::MediaId>::iterator order;
    };

    bool store(const library::MediaId& media, std::shared_ptr<const media::Artwork> artwork,
        media::ArtworkReference reference);
    std::size_t limit_, max_entries_, bytes_{};
    std::list<library::MediaId> order_;
    std::map<library::MediaId, Entry> entries_;
};

} // namespace wiola::app
