/**
 * @file
 * @brief Deterministic bounded ownership of encoded artwork.
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

#include "artwork_store.hpp"

#include <stdexcept>

namespace wiola::app {

ArtworkStore::ArtworkStore(std::size_t bytes, std::size_t entries)
    : limit_{bytes}
    , max_entries_{entries}
{
    if (!bytes || !entries)
        throw std::invalid_argument{"invalid artwork store limits"};
}

void ArtworkStore::remove(const library::MediaId& media)
{
    const auto found = entries_.find(media);

    if (found == entries_.end())
        return;

    if (found->second.bytes)
        bytes_ -= found->second.bytes->data.size();

    order_.erase(found->second.order);
    entries_.erase(found);
}

media::ArtworkReference ArtworkStore::get(const library::MediaId& media)
{
    const auto found = entries_.find(media);

    if (found == entries_.end())
        return {};

    order_.splice(order_.end(), order_, found->second.order);

    return found->second.reference;
}

bool ArtworkStore::put(const library::MediaId& media, std::shared_ptr<const media::Artwork> artwork)
{
    if (artwork &&
        (artwork->empty() || artwork->data.size() > media::max_encoded_artwork_bytes ||
            artwork->data.size() > limit_))
        artwork.reset();
    return store(media, artwork, media::ArtworkReference{artwork});
}

bool ArtworkStore::fail(const library::MediaId& media)
{
    return store(media, {}, media::ArtworkReference::failed());
}

bool ArtworkStore::store(const library::MediaId& media,
    std::shared_ptr<const media::Artwork> artwork, media::ArtworkReference reference)
{
    if (!media.valid())
        return false;

    if (const auto found = entries_.find(media);
        found != entries_.end() && found->second.reference == reference) {
        order_.splice(order_.end(), order_, found->second.order);
        return false;
    }

    remove(media);

    const auto incoming = artwork ? artwork->data.size() : 0;

    while (!entries_.empty() && (entries_.size() >= max_entries_ || bytes_ + incoming > limit_))
        remove(order_.front());

    order_.push_back(media);
    entries_.emplace(media, Entry{std::move(artwork), reference, std::prev(order_.end())});
    bytes_ += incoming;

    return true;
}

} // namespace wiola::app
