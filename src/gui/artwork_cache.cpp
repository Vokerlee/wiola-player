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

#include "artwork_cache.hpp"

#include <QStyle>

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace wiola::gui {

ArtworkCache::ArtworkCache(const QWidget& styled, ArtworkCachePolicy policy)
    : stub_{styled.style()
              ->standardIcon(QStyle::SP_FileIcon)
              .pixmap(policy.cover_size, policy.cover_size)}
    , policy_{policy}
{
    if (!policy_.valid())
        throw std::invalid_argument{"invalid artwork cache policy"};
}

const QPixmap& ArtworkCache::cover_of(std::uint64_t identity,
    const std::shared_ptr<const media::Artwork>& artwork)
{
    if (const auto found = covers_.find(identity); found != covers_.end())
        return found->second.cover;

    QPixmap art;

    if (artwork && artwork->data.size() <= media::max_encoded_artwork_bytes &&
        artwork->data.size() <= std::numeric_limits<unsigned int>::max())
        art.loadFromData(reinterpret_cast<const uchar*>(artwork->data.data()),
            static_cast<unsigned int>(artwork->data.size()));

    QPixmap cover{art.isNull() ? stub_
                               : art.scaled(policy_.cover_size, policy_.cover_size,
                                     Qt::KeepAspectRatio, Qt::SmoothTransformation)};
    const std::size_t bytes{art.isNull() ? 0U
                                         : static_cast<std::size_t>(cover.width()) *
                static_cast<std::size_t>(cover.height()) * 4U};

    while (!covers_.empty() &&
        (covers_.size() >= policy_.covers_kept || bytes_ + bytes > policy_.cached_bytes))
        forget_oldest(1);

    const auto held =
        covers_.emplace(identity, Entry{.cover = std::move(cover), .bytes = bytes}).first;
    order_.push_back(identity);
    bytes_ += bytes;
    return held->second.cover;
}

void ArtworkCache::forget_oldest(std::size_t count)
{
    for (std::size_t gone = 0; gone < count && !order_.empty(); ++gone) {
        const auto found = covers_.find(order_.front());

        if (found != covers_.end()) {
            bytes_ -= found->second.bytes;
            covers_.erase(found);
        }

        order_.pop_front();
    }
}

} // namespace wiola::gui
