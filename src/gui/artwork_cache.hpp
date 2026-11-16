/**
 * @file
 * @brief Bounded GUI-thread presentation of coordinated track artwork.
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

#include "artwork_cache_policy.hpp"

#include <media/media.hpp>

#include <QPixmap>
#include <QWidget>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>

namespace wiola::gui {

class ArtworkCache {
public:
    ArtworkCache(const QWidget& styled, ArtworkCachePolicy policy = {});

    /// Converts already-read bytes on the GUI thread; missing/malformed artwork is cached too.
    [[nodiscard]] const QPixmap& cover_of(std::uint64_t identity,
        const std::shared_ptr<const media::Artwork>& artwork);

private:
    struct Entry {
        QPixmap cover;
        std::size_t bytes{0};
    };

    void forget_oldest(std::size_t count);

    QPixmap stub_;
    ArtworkCachePolicy policy_;
    std::map<std::uint64_t, Entry> covers_;
    std::deque<std::uint64_t> order_;
    std::size_t bytes_{0};
};

} // namespace wiola::gui
